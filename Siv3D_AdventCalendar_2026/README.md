# Siv3D: AIコーディングの二歩目

**TODO: 簡易版スクリプトが動くか全て確認**

## はじめに

今年はClaude Codeを使ったAIコーディングに挑戦した。

最初は、
AIが実装 → 人間がVisual Studioからビルド・実行 → エラーはコピペでAIに報告
のような構成で進めていた。

さらにAIに任せて人間の作業を減らすため、
もう一歩試したことをこの記事に書いてみることにする。
（記事中のスクリプトは実際に使っているものから簡略化している）

## 環境
* OS: Windows 11
* Siv3D: v0.6.16
* AI: Claude Code
* シェル: Git Bash
* デバッガ: WinDbg
  * `winget install Microsoft.WinDbg`
* Visual Studio 2022
  * MSBuildが必要

`Game` という名前でソリューションを作ったと仮定して話を進める。
Visual Studioは起動する必要はなく、ターミナルと軽量なエディタだけで作業できる。

## ビルドをAIに渡す

AIに自動でビルドをさせるために、
ビルド用のツールとビルドエラーを確認する方法をAIに渡す。

全てskillに書くこともできるが、権限管理や作業漏れ防止がしやすいのでシェルスクリプトにまとめる。
```bash:build.sh
#!/bin/bash
set -euo pipefail
REPO_ROOT=$(git rev-parse --show-toplevel)

VSWHERE="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
MSBUILD=$("$VSWHERE" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | tr -d '\r' | head -1)

# ここに Resource.rc の更新など、ビルド前の定型作業を書く

mkdir -p "$REPO_ROOT/logs"
"$MSBUILD" "$REPO_ROOT/Game/Game.sln" -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo \
  -fl "-flp:logfile=$REPO_ROOT/logs/build.log;verbosity=detailed"

echo "Build SUCCEEDED"
```

MSBuildを探して実行。ログはコンテキストを圧迫するので build.log に入れておく。

ビルド前の定型作業もここに書けば漏れなく作業ができ、トークンも節約できる。
例）画像や音声アセットを[Resource.rc](https://siv3d.github.io/ja-jp/tutorial3/resource/#5721-windows)に追記する

これをCLAUDE.mdや実装用skill、テスト用agentなどに教えておけばビルドを自動化できる。

```md:SKILL.md
ビルドは `./tools/build.sh` を使う。
失敗したときは logs/build.log を確認。
```

## テストプレイ時の例外をAIに渡す

実行時の例外（設定ファイルの不備など）が起こったときAIが自動で調査を始めるように、
デバッガ用のツールと例外の情報をAIに渡す。

* cdb（WinDbg のコマンドライン版）を使用
  * 今回は細かくブレークポイントを配置するようなことはせず例外の検知だけに使う
* 想定外のエラーは `throw` で投げる
  * 投げられた時点でダンプを取り、AIに原因調査をさせる
  * `std::expected` で返す形式だと、失敗した瞬間のダンプが取れない
* `catch` されても例外は調査対象として扱う
  * `try-catch` はリカバリ用に留め、正常系は `Optional` 等で表現する

まずはcdbコマンドでデバッグ実行し、例外が発生したらダンプを保存するスクリプトを作る。
```bash:testplay.sh
#!/bin/bash
set -euo pipefail

REPO_ROOT=$(git rev-parse --show-toplevel)
EXE="$REPO_ROOT/Game/App/Game(debug).exe"
PDB_DIR="$REPO_ROOT/Game/Intermediate/Game/Debug"
LOG_DIR="$REPO_ROOT/logs"
DUMP="$LOG_DIR/crash.dmp"

mkdir -p "$LOG_DIR"
rm -f "$DUMP"

CONFIG=$(mktemp)
trap 'rm -f "$CONFIG"' EXIT
ON_CRASH=".lastevent; .dump /ma /o $DUMP; q"

cat > "$CONFIG" <<EOF
sxi out
sxi ld
sxe -c "$ON_CRASH" av
sxe -c "$ON_CRASH" dz
sxe -c "$ON_CRASH" bpe
sxe -c "$ON_CRASH" eh
sxe -c "$ON_CRASH" -c2 "$ON_CRASH" sbo
sxe -c "$ON_CRASH" sov
sxe -c "$ON_CRASH" ii
sxe -c "$ON_CRASH" *
g
q
EOF

cdbX64.exe -G -y "$PDB_DIR;srv*" -cf "$(cygpath -m "$CONFIG")" "$EXE" 2>&1 | iconv -f CP932 -t UTF-8

if [[ -f "$DUMP" ]]; then
  echo "ERROR: ダンプ: $DUMP"
  exit 1
fi
echo "OK"
```

次にダンプを使った原因調査用のスクリプトを作る。
今回は簡潔に書くが、実際には例外メッセージの抽出やスタックの整形も行い、AIに渡す出力を短くしている。
```bash:read-dump.sh
#!/bin/bash
set -euo pipefail

REPO_ROOT=$(git rev-parse --show-toplevel)
DUMP="$REPO_ROOT/logs/crash.dmp"
PDB_DIR="$REPO_ROOT/Game/Intermediate/Game/Debug"

COMMAND=${1:-".exr -1; kn"}

cdbX64.exe -z "$DUMP" -y "$PDB_DIR;srv*" -c ".lines -e; $COMMAND; q" < /dev/null 2>&1 \
  | iconv -f CP932 -t UTF-8
```

テストプレイ用のskillに組み込んでダンプの確認方法をAIに教える。
プレイ中もAIに仕様確認などがしたいので、バックグラウンドで起動させる。
```md:SKILL.md
`run_in_background: true` を指定して `./tools/testplay.sh` を Bash で起動する。
ダンプのパスが出力されたら `./tools/read-dump.sh` を使い、掘り下げたいときは引数にcdbコマンドを指定する。
（例：`.frame <番号>; dv /t` で指定フレームのローカル変数）
```

例外＝調査すべき異常として扱うので、正常に動いているとき常にダンプが0件であることが望ましいが、
外部ライブラリにも `throw` と `try-catch` を使っているものがあるので、そこは回避するための工夫が必要になってしまう。

<details>
<summary> Siv3D: TOMLValueでの問題と回避策 </summary>

設定ファイルでデフォルト値を設定しようとしたときの例

```toml
[Hero]
attack = 2.0
speed = 1.5

[Murabito]
speed = 1.0
```

```cpp
struct Person {
  double attack;
  double speed;

  static Person FromToml(const TOMLValue& toml) {
    return Person{
      .attack = toml[U"attack"].getOr<double>(0.0),
      .speed = toml[U"speed"].getOr<double>(1.0),
    };
  }
};
```

`attack` が設定されていない場合は `0.0` としておきたい。
しかしこの `toml[U"attack"]` が内部で `std::out_of_range` の `throw` を行い、
それを `try-catch` して空の `TOMLValue` を返すような仕様になっている。
村人を作る度に調査が始まってしまう状態であり、これは避けたい。

いくつか解決策があると思うが、以下で紹介されているTOMLパーサを使うときれいに解決できる。
v0.8ではこの仕様が標準になる予定なので、今から慣れておいてもいいと思う。
https://zenn.dev/link/comments/ea0db862ab0e78

</details>

## テストプレイ時のスクショをAIに渡す

Siv3Dでは特定のキーを押すとスクリーンショットを撮ることができる。
以下2点を設定しておくと安心。
* デフォルトでは F12 と PrintScreen だが、環境次第ではどちらも動かない
  * F12 はデバッガが反応し、ブレークポイント例外が発生する
  * PrintScreen はWindowsが反応し、キャプチャツールが起動する
* 保存先のディレクトリ
  * 未設定だとカレントディレクトリが保存先になるが、AIが移動する可能性がある
  * 設定されるとexeからの相対パスになり、exeが決まった位置にあれば揺れない

```cpp
ScreenCapture::SetShortcutKeys({ KeyP }); // ゲーム操作と衝突しないキー
ScreenCapture::SetScreenshotDirectory(U"Screenshot/"); // Game/App/Screenshot/
```

スクリーンショットを残しておく必要がないのであればcdb実行前にクリアしておく。
```bash:testplay.sh
# cdb実行の手前
rm -f "$REPO_ROOT/Game/App/Screenshot/"*
```

テストプレイ用のskillにスクショの確認方法も書いておく。
```md:SKILL.md
（testplay.shの説明の後）
テストプレイ後、またはスクリーンショットについて指摘があったとき、Game/App/Screenshot/ にスクリーンショットがあれば確認する。
```

AIが自動でスクリーンショットを撮って画面を確認するための設定も作る。
```cpp
void Main() {
  ScreenCapture::SetShortcutKeys({ KeyP });
  ScreenCapture::SetScreenshotDirectory(U"Screenshot/");

  const bool captureMode = System::GetCommandLineArgs().contains(U"--capture");
  bool captured = false;
  while (System::Update()) {
    if (captured) { break; } // キャプチャの保存のため1フレーム待つ

    // 略（ゲーム本体の更新）

    if (captureMode && 1.0 <= Scene::Time()) {
      ScreenCapture::SaveCurrentFrame();
      captured = true;
    }
  }
}
```

```bash:capture.sh
#!/bin/bash
set -euo pipefail

REPO_ROOT=$(git rev-parse --show-toplevel)
SCREENSHOT_DIR="$REPO_ROOT/Game/App/Screenshot"
EXE="$REPO_ROOT/Game/App/Game(debug).exe"

rm -f "$SCREENSHOT_DIR/"*
"$EXE" --capture
ls "$SCREENSHOT_DIR/"*.png
```

capture.shをskillに教えることで、AIが自分で画面を確認することができる。
ただ、「ゲームを起動したら撮りたいシーンにすぐに飛べる」ことが前提になっているため別途その仕組みが必要。
（私は[去年の記事](https://qiita.com/mori08/items/8f140932a0b89b74b1e5)のような設計を応用して好きな場面にすぐ飛べるようにした）

任意の撮りたいシーンへの遷移を全て自動化するのは難しいので手動と上手く使い分ける。

## おわりに

下のリポジトリで試したことを記事化してみました。
https://github.com/mori08/Ash2

開発サイクルにAIを組み込んでいくのは、ゲームでより良い自動周回を考えているようで楽しかったです。
面倒で手を出していなかったもの（フォーマッタ、静的解析、自動テストなど）も
AIに手伝ってもらいながら全て導入することができました。

ただ、上記のような整備やリファクタリングが手軽になってしまうのも考えもので、
「個人開発でそこまで必要か」
「ゲームを完成させることより、AIでの最適化そのものが目的化していないか」
あたりは注意しないといけないと感じています。

記事内のやり方はまだまだ試行錯誤中のものなので、指摘等あればお願いします。

<details>
<summary> 余談 </summary>

ゲーム制作においてAIを何に使うか・どのように使うか・どこまで使うか、今考えていることを書いてみる。

**ゲームデザイン**
AIの役割：ラバーダック

AIに「こんなゲームが作りたい」ということを捲し立てている。
面白さの言語化や新しいアイデアのきっかけ作りのためにとにかく話す。
AIが意見を言ってくることがあるが9割は蹴って、壁打ちの相手として使っている。


**コーディング**
AIの役割：エンジニア

ソースコードを全てAIに書かせ始めている。設計も細かい箇所であればAIに任せる。
私は土台となる設計やissueを進める順番の決定、AIの書いたplanや実装のレビューなどを行う。
実装はできるだけ理解しておきたいのでコードはほぼ全て読んでいる。

コーディングは他作業より少し特殊で、個性や作家性が最も評価されづらい。
読みやすくて安全なコードは、より一般的で無個性でベストプラクティスに則ったものだと思う。
このあたりは人間よりAIの方が得意なので助かる。


**グラフィック**
AIの役割：動かすだけ

画像素材はAIに作らせていない。理由は沢山ある。
* 現時点ではClaudeに画像を生成する機能がない
* 知識不足で「もっとこうしてほしい」をうまく説明できない
* 低解像度のドット絵の場合、欲しい絵を説明するより描いた方が速い
* AIっぽいイラストを好きになれない
* 描きたい

あまり良いアドバイスが返ってこないので、レビューもさせていない。

グラフィックに一切AIが関わっていないかと言われるとそうでもない。
「フワフワ動かして」と言ったとき数式を提案してくれるのはAIであり、
それらもグラフィックを構成する一つの要素だと思っている。


**サウンド**
AIの役割：教師

音素材もAIに作らせていない。
『「もっとこうしてほしい」をうまく説明できない』が特に強い分野で、
知識もセンスもないのでAIに指摘できないし、他人に依頼もできない。
フリーBGMはどう選べばいいのかも分からないので無音のゲームを作り続けていた時期もあった。

今は作曲の易しい本を読んだ上で、Dominoを使って曲作りに挑戦している。
AIにコード進行などのアドバイスをもらいながら進めるが、
手を動かす所、聞いて判断する所は人間がちゃんとやる。


**シナリオ**
AIの役割：校正

全て自分で考えて書いている。
AIには後から読ませて誤字脱字・分かりづらい表現・矛盾などがないかだけ確認させていて、
この記事もこの形で書いた。

文章を書くことに苦手意識があるのでコーディングのように任せたいが、
AIが出力する日本語には違和感（AIっぽさ？）のようなものがある気がしている。
自分用ドキュメント程度であれば別にいいが、シナリオにそれが混ざるのが嫌なので自分で書く。


**どう向き合うか**
そもそもAIについては思うところがあり、ゲーム制作に使うのかは結構悩んだ。
複数の軸があるため、前述の通り場面ごとに使い方も一貫していない。

AIは重めの問題をいくつか抱えている。
資源、著作権、雇用、不正アクセスあたりが最近でも話題になっており、
AIは人間を総合的には良い方向に持っていかないのでは？とも思っている。

それはそれとして、仕事ではAIを使っていて、開発スピードは確実に上がっている。
周りに置いていかれないために趣味のゲーム制作を練習の場に選んだという背景もある。
ロボット・AI・アンドロイド系SFが好きなので、機械と対話しながら開発が進んでいくという体験は本当に楽しかった。

健康に悪いと知っているが、酒は飲む。
環境に悪いと知っているが、電気は使う。
私のAI利用の感覚はこのあたりに近いかもしれない。

</details>

