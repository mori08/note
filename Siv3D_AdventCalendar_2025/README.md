# Siv3D:設定ファイルにイベント内容を切り出す
探索やバトルの合間に会話イベント等を入れるとき、
イベントの流れやセリフを書き換える度にコンパイルはしたくない。

ゲームの進行を設定ファイルに切り分け、
制作終盤では設定ファイルを編集するだけでゲームが作り上げられるようにする。

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

Siv3Dでコードを書きながら考える

## （１）文字列を使ってゲーム内の全オブジェクトを操作できるようにする
設定ファイルからは `object="player"` のように文字列でオブジェクトを指定する。

`HashTable<String, オブジェクト（またはその参照）>` のようなデータ構造が必要になるが、オブジェクトには様々な種類があって一つにまとめづらい。

ECS(Entity Component System)のような考え方でこれを実現する。  
EnTTなどを使うとより良いと思うが、
話を簡単にするために名前→コンポーネントの `HashTable` だけで実装する。

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
struct EntityManager
{
	// Entityの名前セット
    HashSet<String> nameSet;

    // 名前 -> Component
    HashTable<String, PosComponent> posTable;
    HashTable<String, ImageComponent> imageTable;
    HashTable<String, TextComponent> textTable;

    void erase(const String& name)
    {
        nameSet.erase(name);
        posTable.erase(name);
        imageTable.erase(name);
        textTable.erase(name);
    }
};
```

画像とテキストを試しに表示してみる。

```cpp
Game::Game()
{
	// プレイヤーを追加したい
	const String name1 = U"player";
	m_entitys.nameSet.insert(name1);
	m_entitys.posTable[name1] = {
		Vec3{ 100, 100, 0 },
	};
	m_entitys.imageTable[name1] = {
		Texture{ U"siv3Dkun.png" },
		Size{ 80, 80 },
		Point{ 0, 0 },
	};

	// テキストを表示したい
	const String name2 = U"text";
	m_entitys.nameSet.insert(name2);
	m_entitys.posTable[name2] = {
		Vec3{ 100, 140, 1 },
	};
	m_entitys.textTable[name2] = {
		U"テスト",
		Font{ 20 },
	};
}

void Game::update()
{
	// TODO
}

void Game::draw() const
{
	// TODO: z座標でソートして描画
	for (const auto& name : m_entitys.nameSet)
	{
		if (not m_entitys.posTable.count(name)) { continue; }
		const auto& posC = m_entitys.posTable.at(name);

		// 画像の表示
		if (m_entitys.imageTable.count(name))
		{
			const auto& imageC = m_entitys.imageTable.at(name);
			if (not imageC.isHidden)
            {
                imageC.texture(imageC.imagePos * imageC.imageSize, imageC.imageSize).drawAt(posC.pos.xy());
            }
		}

		// テキストの表示
		if (m_entitys.textTable.count(name))
		{
			const auto& textC = m_entitys.textTable.at(name);
			textC.font(textC.text).drawAt(posC.pos.xy());
		}
	}
}

void Main()
{
    Window::Resize(Size{ 640, 480 });
    Scene::SetBackground(Color(0xf0));
	Game game;
	while (System::Update())
	{
		game.update();
		game.draw();
	}
}
```

![Image1](image1.png)

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











## おわりに
ゲーム作りはほぼ独学でやっていて他の人の実装を見ることがほぼない。  
これから書く内容が記事にするほどでもないほどありふれているor記事にすべきでないほどずれている可能性もある

だけど、考えたことを共有することに何か価値があると信じて書いてみました

