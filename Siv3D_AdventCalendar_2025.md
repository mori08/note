# Siv3D:設定ファイルにイベント内容を切り出す
探索やバトルの合間に会話イベント等を入れるとき、
イベントの流れやセリフを書き換える度にコンパイルをし直すのは辛い

ゲーム進行を設定ファイルにできるだけ切り分け、
制作終盤では設定ファイルを編集するだけでゲームが作り上げられるようにしていきたい

```toml
# 例 NPCに話しかけたときのイベント #
[[Scenario1]]
push = "text"
param = {entity="player", text="こんにちは"}
[[Scenario1]]
push = "wait"
param = 1.0
[[Scenario1]]
push = "text"
param = {entity="npc", text="こんにちは"}
[[Scenario1]]
push = "text"
param = {entity="npc", text="ついてきて"}
[[Scenario1]]
push = "walk"
param = {entity="npc", direction="right", length=100}
[[Scenario1]]
push = "walk"
param = {entity="npc", direction="down", length=100}
```

これを実現するために考えたことをSiv3Dでコードを書きながら共有します

## 1 外部ファイルで文字列を使って文字列を指定する
外部ファイルからは `object="player"` のように文字列で指定する

文字列から全てのオブジェクトを編集できてほしいので、
`HashTable<String, オブジェクト（またはその参照）>` のようなデータ構造が必要になる
が、オブジェクトには様々な種類があって一つにまとめづらい

万能オブジェクトクラスを作る、`*ObjectBase` にその派生クラスを登録する、のような手法もあるが
ここではECS(Entity Component System)のような考え方で実現する

```cpp
// 座標
struct PosComponent
{
	Vec3 pos; // {x,y}: 画面上の位置, z: 描画順
};

// 画像表示
struct ImageComponent
{
	Texture texture;
	Size imageSize; // 画像を切り分けるサイズ
	Point imagePos; // 表示する画像の番号
};

// テキスト表示
struct TextComponent
{
	String text;
    Font font;
};


struct EntitySet
{
	// Entityの名前セット
    HashSet<String> nameSet;

    // 名前 -> Component
    HashTable<String, PosComponent> posTable;
    HashTable<String, ImageComponent> imageTable;
    HashTable<String, TextComponent> textTable;
};
```

EnTTなどを使った方がいいのかもしれないが、
この記事では話を簡単にするために、名前→コンポーネントの `HashTable` で実装する


```cpp
// name="player" で entityを追加する例を書く
```
























## おわりに
ゲーム作りはほぼ独学でやっていて他の人の実装を見ることがほぼない
これから書く内容が記事にするほどでもないほどありふれているor記事にすべきでないほどずれている可能性もある

だけど、考えたことを共有することに何か価値があると信じて書いてみました

