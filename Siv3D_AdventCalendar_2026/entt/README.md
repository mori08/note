# Siv3D: ECSをちゃんとやる

## はじめに

ゲーム内で「モノ」はどのように管理するべきだろうか。

一括で管理するなら `Array<Object*>` のような方法をとりたくなるが、
作りたいものが複雑になると `Object` が肥大化し扱うのが難しくなっていく。

今回はECS（Entity Component System）という、実体（Entity）にデータ（Component）を追加し、対応するロジック（System）で動かす設計を採用してより柔軟に対処できるようにする。

## EnTT

去年の記事ではかなり簡単なECSライクな設計を使っていた。
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

## Entityを描画してみる

まずは座標用のコンポーネント。

```cpp
struct Position {
  Vec3 pos; // {x,y}: 画面上の位置, z: 描画順
};
```

次に描画用コンポーネント。
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
