#include <Siv3D.hpp>

/*
* Entity + Component
*/

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

/*
* State と Stack
*/

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

	virtual void onAfterPush(EntitySet& entities) = 0;
	virtual StackOp update(EntitySet& entities) = 0;
	virtual void onBeforePop(EntitySet& entities) = 0;
};

class TestState : public State
{
public:
	TestState(int32 arg) { m_time = arg; }
	TestState(const TOMLValue& param) { m_time = param.get<int32>(); }
	void onAfterPush(EntitySet&) override { Print << U"push: " << m_time; }
	StackOp update(EntitySet&) override { return --m_time < 0 ? StackOp::Replace(std::make_unique<TestState>(120)) : StackOp::None(); }
	void onBeforePop(EntitySet&) override { Print << U"pop"; }

private:
	int32 m_time;
};

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
	m_now = reader[scenarioName].tableArrayView().begin();
	m_end = reader[scenarioName].tableArrayView().end();
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
		{ U"test", getMakeStateFunc<TestState>(), },
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

// Stateの管理
class StateStack
{
public:
	StateStack(EntitySet& entities);
	void update(EntitySet& entities);

private:
	void pop(EntitySet& entities);
	void push(EntitySet& entities, std::unique_ptr<State>&& nextState);

	// top以外のデータも見たいのでArrayで実装
	// 末尾以外のデータを編集しないように気を付ける
	Array<std::unique_ptr<State>> m_stack;
};

StateStack::StateStack(EntitySet& entities)
{
	push(entities, std::make_unique<ScenarioState>(U"init"));
}

void StateStack::update(EntitySet& entities)
{
	if (m_stack.empty()) { return; }

	auto [type, nextState] = m_stack.back()->update(entities);

	switch (type)
	{
	case StackOp::Type::NONE:
		break;

	case StackOp::Type::POP:
		pop(entities);
		break;

	case StackOp::Type::PUSH:
		push(entities, std::move(nextState));
		break;

	case StackOp::Type::REPLACE:
		while (not m_stack.empty()) { pop(entities); }
		push(entities, std::move(nextState));
		break;
	}
}

void StateStack::pop(EntitySet& entities)
{
	m_stack.back()->onBeforePop(entities);
	m_stack.pop_back();
}

void StateStack::push(EntitySet& entities, std::unique_ptr<State>&& nextState)
{
	m_stack.push_back(std::move(nextState));
	m_stack.back()->onAfterPush(entities);
}

/*
* EntitySet と StateStack を Game でまとめて管理
*/

class Game
{
public:
	Game();
	void update();
	void draw() const;

private:
	EntitySet m_entities;
	StateStack m_states; // 追加
};

Game::Game()
	: m_states{ m_entities }
{
}

void Game::update()
{
	m_states.update(m_entities);
}

void Game::draw() const
{
	// TODO: z座標でソートして描画
	for (const auto& name : m_entities.nameSet)
	{
		if (not m_entities.posTable.count(name)) { continue; }
		const auto& posC = m_entities.posTable.at(name);

		// 画像の表示
		if (m_entities.imageTable.count(name))
		{
			const auto& imageC = m_entities.imageTable.at(name);
			if (not imageC.isHidden)
			{
				imageC.texture(imageC.imagePos * imageC.imageSize, imageC.imageSize).drawAt(posC.pos.xy());
			}
		}

		// テキストの表示
		if (m_entities.textTable.count(name))
		{
			const auto& textC = m_entities.textTable.at(name);
			textC.font(textC.text).drawAt(posC.pos.xy(), Palette::Black);
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
		if (KeySpace.down())
		{
			ScreenCapture::SaveCurrentFrame(U"screenshot.png");
		}
	}
}
