# Siv3D: ECSを使う

## はじめに

ゲーム内で「モノ」はどのように管理するべきだろうか。

一括で管理するなら `Array<Object*>` のような方法をとりたくなるが、
作りたいものが複雑になると `Object` が肥大化し扱うのが難しくなっていく。

今回はECS（Entity Component System）という、実体（Entity）にデータ（Component）を追加し、対応するロジック（System）で動かす設計を採用してより柔軟に対処できるようにする。

## EnTT

去年の記事では簡易的なECSライクな設計を使っていた。
https://qiita.com/mori08/items/8f140932a0b89b74b1e5#１文字列とゲーム内の物を紐づける

今回はもっと本格的にC++のECSライブラリ「EnTT」を使ってみる。
以下のシングルファイルから使ってみるか、vcpkgで導入する。
https://github.com/skypjack/entt/blob/v3.16.0/single_include/entt/entt.hpp

### 使い方

`entt::registry` が Entity や Component の管理をする。
何をするにもこれが土台になる。

```cpp
entt::registry registry;
```

Entity の作成と破壊

```cpp
const entt::entity entity = registry.create();
registry.destroy(entity);
```

Component の付け外し

```cpp
// struct Hoge { int x; };

const entt::entity entity = registry.create();
registry.emplace<Hoge>(entity, Hoge{ .x = 1 });
registry.remove<Hoge>(entity);
```

System の実装（Componentへの参照と更新）

```cpp
void HogeSystem::Update(entt::registry& registry) {
  for (auto [entity, hoge] : registry.view<Hoge>().each()) {
    hoge.x += 1;
  }
}
```

## 去年のデモを作り直す

EnTTで管理が楽になったので、Componentをどんどん増やしながら再構築する。

https://x.com/nanka_mori/status/1991910174991520013

### 描画

まずは座標Component

```cpp
struct Position {
  Vec3 pos; // {x, y}: 画面上の座標、z: 奥行き
};
```

次に描画用Component
画像用とテキスト用の型を `std::variant` を使ってまとめる。

```cpp
// 画像用
struct ImageDrawable {
  Texture texture;
  Size imageSize; // 画像を切り分けるサイズ
  Point imagePos; // 表示する画像の番号
};

// テキスト用
struct TextDrawable {
  Font font;
  String text;
};

// 描画用コンポーネント
using Drawable = std::variant<ImageDrawable, TextDrawable>;
```

描画用のシステムを作る

```cpp
namespace DrawSystem {

void DrawOne(const ImageDrawable& d, const Vec2& pos) {
  d.texture(d.imagePos * d.imageSize, d.imageSize).drawAt(pos);
}

void DrawOne(const TextDrawable& d, const Vec2& pos) {
  d.font(d.text).drawAt(pos, Palette::Black);
}

void Draw(const entt::registry& registry) {
  Array<std::pair<double, entt::entity>> drawList;
  for (auto [entity, position, drawable] : registry.view<Position, Drawable>().each()) {
    drawList.emplace_back(position.pos.z, entity);
  }
  std::ranges::sort(drawList);

  for (const auto& [z, entity] : drawList) {
    const auto& position = registry.get<Position>(entity);
    const auto& drawable = registry.get<Drawable>(entity);
    std::visit([&](const auto& d) { DrawOne(d, position.pos.xy()); }, drawable);
  }
}

}
```

<details>
<summary> 画像とテキストを描画してみる </summary>

```cpp
// (略) ComponentやSystemの定義

void Main() {
  Window::Resize(Size{ 640, 480 });
  Scene::SetBackground(Color(0xf0));

  entt::registry registry;

  { // Playerを追加
    const entt::entity entity = registry.create();
    registry.emplace<Position>(entity, Vec3{100, 100, 0});
    registry.emplace<Drawable>(entity, ImageDrawable{
      .texture = Texture{ U"siv3Dkun.png" },
      .imageSize = Size{ 80, 80 },
      .imagePos = Point{ 0, 0 },
    });
  }

  { // テキストを追加
    const entt::entity entity = registry.create();
    registry.emplace<Position>(entity, Vec3{100, 140, 1});
    registry.emplace<Drawable>(entity, TextDrawable{
      .font = Font{ 20 },
      .text = U"テスト"
    });
  }

  while (System::Update()) {
    DrawSystem::Draw(registry);
  }
}
```

</details>

### 名前

外部ファイルからEntityを指定するための名前を管理する必要があり、
新しく２つの概念を導入して対応する。

Context: registryで管理される共有データ

```cpp
// struct HogeManager{ Array<entt::entity> list; };
registry.ctx().emplace<HogeManager>();
const auto& list = registry.ctx().get<HogeManager>().list;
```

on_construct/on_destroy: Componentが付け外しされたときに走る処理

```cpp
registry.on_construct<Hoge>().connect<[](entt::registry&, entt::entity){
  Print << U"Hogeを付けた";
}>();
registry.on_destroy<Hoge>().connect<[](entt::registry&, entt::entity){
  Print << U"Hogeを外した";
}>();
```

これらを使って、
「Contextで名前->Entityのマップを持ち、Componentの追加/削除で自動で更新していく」
という状態を作っていく。

まずは型の準備

```cpp
struct Name {
  // 後から変更されるとLookupと不整合を起こすので const
  const String value;
};

using NameLookup = HashTable<String, entt::entity>;
```

`const` を使うとenttで並び替えられないコンポーネントとして扱われ `sort` が使えなくなるが、
今回は使わないので問題ない。

次はregistryへの登録

```cpp
registry.ctx().emplace<NameLookup>();

registry.on_construct<Name>().connect<[](entt::registry& registry, entt::entity entity){
  // 重複チェックは今回スキップ
  registry.ctx().get<NameLookup>()[registry.get<Name>(entity).value] = entity;
}>();
registry.on_destroy<Name>().connect<[](entt::registry& registry, entt::entity entity){
  registry.ctx().get<NameLookup>().erase(registry.get<Name>(entity).value);
}>();
```

これで、`Name` コンポーネントの追加で自動で `NameLookup` に登録される。

```cpp
const entt::entity entity = registry.create();
registry.emplace<Name>(entity, U"player");
```

### State系の処理を書き直す

TODO: githubリンク