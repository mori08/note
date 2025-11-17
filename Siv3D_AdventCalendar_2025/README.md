# Siv3D:シナリオは設定ファイルに

## はじめに
ゲームのストーリーはコードに書きたくない。
できるだけ設定ファイルに書いて、それを読みながら動いてほしい。

下のTOMLは簡単なイベントについて書いたもの。
C++で「イベント(`event`)」だと他の概念と衝突しやすそうなので、以降は「シナリオ(`scenario`)」と呼ぶ

```toml
# 例 NPCに話しかけたときのシナリオ #
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

これを動かすためにはどんな設計が必要か、Siv3Dを書きながら考える。

## （１）文字列とゲーム内の物を紐づける
設定ファイルからは `entity="player"` のように文字列でゲーム内の物を指定する。
文字列でどんな物でも指定できる設計にしておきたい。

今回はECS(Entity Component System)のような考え方でやってみる。
作るコンポーネントは3種類（座標、画像、テキスト）、
話を簡単に進めるために名前→コンポーネントのシンプルな `HashTable` で実装した。

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
	bool isHidden = false; // true のとき非表示
};

// テキスト表示
struct TextComponent
{
	String text;
	Font font;
};

// EntityとComponentの管理
struct EntitySet
{
	// Entity名のセット
	HashSet<String> nameSet;

	// 名前 -> Component
	HashTable<String, PosComponent> posTable;
	HashTable<String, ImageComponent> imageTable;
	HashTable<String, TextComponent> textTable;

	// Entityの削除
	void erase(const String& name)
	{
		nameSet.erase(name);
		posTable.erase(name);
		imageTable.erase(name);
		textTable.erase(name);
	}
};
```

<details>
<summary> 画像とテキストを試しに表示してみる。 </summary>

Entityを1つ描画する関数を用意
```cpp
void drawEntity(const EntitySet& entities, const String& name)
{
	if (not entities.posTable.count(name)) { return; }
	const auto& posC = entities.posTable.at(name);

	// 画像の表示
	if (entities.imageTable.count(name))
	{
		const auto& imageC = entities.imageTable.at(name);
		if (not imageC.isHidden)
		{
			imageC.texture(imageC.imagePos * imageC.imageSize, imageC.imageSize).drawAt(posC.pos.xy());
		}
	}

	// テキストの表示
	if (entities.textTable.count(name))
	{
		const auto& textC = entities.textTable.at(name);
		textC.font(textC.text).drawAt(posC.pos.xy(), Palette::Black);
	}
}
```

複数のEntityをz座標順で描画する関数を用意
```cpp
void drawEntities(const EntitySet& entities)
{
	Array<std::pair<double, String>> drawList;
	for (const auto& name : entities.nameSet)
	{
		if (entities.posTable.count(name))
		{
			const auto& posC = entities.posTable.at(name);
			drawList.emplace_back(posC.pos.z, name);
		}
	}
	std::sort(drawList.begin(), drawList.end());

	for (const auto& [z, name] : drawList)
	{
		drawEntity(entities, name);
	}
}
```

Player画像とテキストのEntityを追加して描画
```cpp
void Main()
{
	Window::Resize(Size{ 640, 480 });
	Scene::SetBackground(Color(0xf0));

	EntitySet entities;

	{ // Playerを追加
		const String name = U"Player";
		entities.nameSet.insert(name);
		entities.posTable[name] = { Vec3{100, 100, 0} };
		entities.imageTable[name] = {
			Texture{ U"siv3Dkun.png" },
			Size{80, 80},
			Point{0, 0}
		};
	}

	{ // テキストを追加
		const String name = U"text";
		entities.nameSet.insert(name);
		entities.posTable[name] = { Vec3{ 100, 140, 1 } };
		entities.textTable[name] = { U"テスト", Font{ 20 } };
	}

	while (System::Update())
	{
		drawEntities(entities);
	}
}
```

![Image1](image1.png)

</details>

## （２）ゲームの状態をスタックで持つ
やりたいことを整理する。

* まずゲームの状態を管理、遷移させながらゲームを進行させる
  * Stateパターンを使う
* 設定ファイルを読んでイベントを進行させる `State` がほしい
  * `ScenarioState` を作る
* 別の状態に遷移した後 `ScenarioState` に戻って続きから進行させたい
  * `State` をスタックで管理すれば、popすれば一つ前の状態に戻れる
  * スタック内の `State` の進行状況は保持されたまま
  * `ScenarioState` 以外の箇所でもスタックの方が都合がいいことが多い

まずは `State` をスタックで扱う準備をする。

```cpp
class State;

// Stack操作について（State::update()の戻り値にする）
struct StackOp
{
	enum class Type
	{
		NONE,
		POP,
		PUSH,
		REPLACE, // clear + push
	};

	Type type;
	std::unique_ptr<State> nextState;

	static StackOp None() { return { Type::NONE, nullptr }; }
	static StackOp Pop() { return{ Type::POP, nullptr }; }
	static StackOp Push(std::unique_ptr<State>&& nextState) { return { Type::PUSH, std::move(nextState) }; }
	static StackOp Replace(std::unique_ptr<State>&& nextState) { return { Type::REPLACE, std::move(nextState) }; }
};

// 状態の基底クラス
class State
{
public:
	virtual ~State() = default;

	virtual void onAfterPush(EntitySet& entitys) = 0;
	virtual StackOp update(EntitySet& entitys) = 0;
	virtual void onBeforePop(EntitySet& entitys) = 0;
};

// Stateの管理
class StateStack
{
public:
	StateStack(EntitySet& entitys);
	void update(EntitySet& entitys);

private:
	void pop(EntitySet& entitys);
	void push(EntitySet& entitys, std::unique_ptr<State>&& nextState);

	// top以外のデータも見たいのでArrayで実装
	// 末尾以外のデータを編集しないように気を付ける
	Array<std::unique_ptr<State>> m_stack;
};

StateStack::StateStack(EntitySet& entitys)
{
	// TODO: 初期Stateをpush
}

void StateStack::update(EntitySet& entitys)
{
	if (m_stack.empty()) { return; }

	auto [type, nextState] = m_stack.back()->update(entitys);

	switch (type)
	{
	case StackOp::Type::NONE:
		break;

	case StackOp::Type::POP:
		pop(entitys);
		break;

	case StackOp::Type::PUSH:
		push(entitys, std::move(nextState));
		break;

	case StackOp::Type::REPLACE:
		while (not m_stack.empty()) { pop(entitys); }
		push(entitys, std::move(nextState));
		break;
	}
}

void StateStack::pop(EntitySet& entitys)
{
	m_stack.back()->onBeforePop(entitys);
	m_stack.pop_back();
}

void StateStack::push(EntitySet& entitys, std::unique_ptr<State>&& nextState)
{
	m_stack.push_back(std::move(nextState));
	m_stack.back()->onAfterPush(entitys);
}
```

```cpp
// GameクラスにStateStackを追加
class Game
{
public:
	Game();
	void update();
	void draw() const;

private:
	EntitySet m_entitys;
	StateStack m_states; // 追加
};

void Game::update()
{
	m_states.update(m_entitys);
}
```

次に設定ファイル(scenario.toml)を読む `ScenarioState` を作って以下の機能を持たせる。

* Entityを作ることができる
	* pop時に作成したEntityを削除する
* scenario.tomlを読み込む
	* 指定した `TableArray` を順に読む
	* `make` でEntity作成
	* `push=` `replace=` で他 `State` を作る

```cpp
// scenario.tomlを読み込んで他Stateをpush
class ScenarioState : public State
{
public:
	using MakeStateFunc = std::function<std::unique_ptr<State>(const TOMLValue&)>;

	ScenarioState(const String& scenarioName);
	ScenarioState(const TOMLValue& param);

	void onAfterPush(EntitySet& entities) override;
	StackOp update(EntitySet& entities) override;
	void onBeforePop(EntitySet& entities) override;

private:
	void makeEntities(EntitySet& entities, const TOMLValue& params);
	template<typename Type> static MakeStateFunc getMakeStateFunc();

	// シナリオ管理
	TOMLTableArrayIterator m_now;
	TOMLTableArrayIterator m_end;

	// ここで作ったEntityの名前
	HashSet<String> m_nameSetMadeOnThis;
};

ScenarioState::ScenarioState(const String& scenarioName)
{
	static const TOMLReader reader{ U"scenario.toml" };
	m_now = toml()[scenarioName].tableArrayView().begin();
	m_end = toml()[scenarioName].tableArrayView().end();
}

ScenarioState::ScenarioState(const TOMLValue& param)
	: ScenarioState{ param.getString() }
{
}

void ScenarioState::onAfterPush(EntitySet&)
{
}

StackOp ScenarioState::update(EntitySet& entities)
{
	// 最後まで読んだら pop
	if (m_now == m_end)
	{
		return StackOp::Pop();
	}

	// 文字列 -> 次のStateを作る関数へ変換
	static const HashTable<String, MakeStateFunc> MAKE_TABLE = {
		// ここにpushしたいStateを追加
		// 例: { U"hoge", MakeStateFunc<HogeState>() },
	};

	TOMLValue nowToml = *m_now;
	++m_now;
	if (nowToml[U"make"].isTableArray())
	{
		makeEntities(entities, nowToml[U"make"]);
		return StackOp::None();
	}
	if (nowToml[U"push"].isString())
	{
		const String stateName = nowToml[U"push"].getString();
		return StackOp::Push(MAKE_TABLE.at(stateName)(nowToml[U"param"]));
	}
	if (nowToml[U"replace"].isString())
	{
		const String stateName = nowToml[U"replace"].getString();
		return StackOp::Replace(MAKE_TABLE.at(stateName)(nowToml[U"param"]));
	}

	return StackOp::None();
}

void ScenarioState::onBeforePop(EntitySet& entities)
{
	for (const auto& name : m_nameSetMadeOnThis)
	{
		entities.erase(name);
	}
}

void ScenarioState::makeEntities(EntitySet& entities, const TOMLValue& params)
{
	for (const auto& param : params.tableArrayView())
	{
		const String name = param[U"name"].getString();
		entities.nameSet.insert(name);
		m_nameSetMadeOnThis.insert(name);

		if (param[U"pos"].isTable())
		{
			TOMLValue pos = param[U"pos"];
			entities.posTable[name] = {
				Vec3{
					pos[U"x"].get<double>(),
					pos[U"y"].get<double>(),
					pos[U"z"].get<double>(),
				}
			};
		}

		if (param[U"image"].isTable())
		{
			TOMLValue image = param[U"image"];
			entities.imageTable[name] = {
				Texture{ image[U"path"].getString() },
				Size{
					image[U"size.x"].get<int32>(),
					image[U"size.y"].get<int32>(),
				},
				Point{
					image[U"pos.x"].get<int32>(),
					image[U"pos.y"].get<int32>(),
				},
				image[U"isHidden"].getOr<bool>(false),
			};
		}

		if (param[U"text"].isTable())
		{
			TOMLValue text = param[U"text"];
			entities.textTable[name] = {
				text[U"text"].getString(),
				Font{ text[U"font.size"].get<int32>() },
			};
		}
	}
}

template<typename Type>
ScenarioState::MakeStateFunc ScenarioState::getMakeStateFunc()
{
	return [](const TOMLValue& param) {return std::make_unique<Type>(param); };
}
```

`StateStack` の初期状態は init で登録する。

```cpp
StateStack::StateStack(EntitySet& entities)
{
	push(entities, std::make_unique<ScenarioState>(U"init"));
}
```

他のState系クラスも作る

<details>
<summary> WaitState（指定した秒数待つ） </summary>

```toml
[[Scenario]]
	push = "wait"
	param = {time=1.0}
```

```cpp
class WaitState : public State
{
public:
	WaitState(const TOMLValue& param);

	void onAfterPush(EntitySet& entities) override;
	StackOp update(EntitySet& entities) override;
	void onBeforePop(EntitySet& entities) override;

private:
	double m_time;
};
```

```cpp
WaitState::WaitState(const TOMLValue& toml)
	: m_time{ param.get<double>() }
{
}

void WaitState::onAfterPush(EntitySet&)
{
}

StackOp WaitState::update(EntitySet&)
{
	m_time -= Scene::DeltaTime();
	return m_time < 0 ? StackOp::Pop() : StackOp::None();
}

void WaitState::onBeforePop(EntitySet&)
{
}
```

</details>









## おわりに
ゲーム作りはほぼ独学でやっていて他の人の実装を見ることがほぼない。
これから書く内容が記事にするほどでもないほどありふれているor記事にすべきでないほどずれている可能性もある

だけど、考えたことを共有することに何か価値があると信じて書いてみました

