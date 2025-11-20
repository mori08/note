#include <Siv3D.hpp>


/*
* Entity Component
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


/*
* State
*/

class State
{
public:
	struct Action
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

		static Action None() { return { Type::NONE, nullptr }; }
		static Action Pop() { return{ Type::POP, nullptr }; }
		static Action Push(std::unique_ptr<State>&& state) { return{ Type::PUSH, std::move(state) }; }
		static Action Replace(std::unique_ptr<State>&& state) { return{ Type::REPLACE, std::move(state) }; }
	};

	virtual ~State() = default;

	virtual void onAfterPush(EntitySet& entities) = 0;
	virtual Action update(EntitySet& entities) = 0;
	virtual void onBeforePop(EntitySet& entities) = 0;
};


/*
* WaitState
*/

class WaitState : public State
{
public:
	WaitState(const TOMLValue& param);

	void onAfterPush(EntitySet& entities) override;
	Action update(EntitySet& entities) override;
	void onBeforePop(EntitySet& entities) override;

private:
	Timer m_timer;
};

WaitState::WaitState(const TOMLValue& param)
	: m_timer{ SecondsF(param.get<double>()), StartImmediately::Yes }
{
}

void WaitState::onAfterPush(EntitySet&)
{
}

State::Action WaitState::update(EntitySet&)
{
	return m_timer.isRunning() ? Action::None() : Action::Pop();
}

void WaitState::onBeforePop(EntitySet&)
{
}


/*
* SpeakState
*/

class SpeakState : public State
{
public:
	SpeakState(const TOMLValue& param);

	void onAfterPush(EntitySet& entities) override;
	Action update(EntitySet& entities) override;
	void onBeforePop(EntitySet& entities) override;

private:
	const String m_entityName;
	const String m_text;
	const Vec2 m_offset;
};

SpeakState::SpeakState(const TOMLValue& param)
	: m_entityName{ param[U"entity"].getString() }
	, m_text{ param[U"text"].getString() }
	, m_offset{
		param[U"offset.x"].getOr<double>(0.0),
		param[U"offset.y"].getOr<double>(0.0)
	}
{
}

void SpeakState::onAfterPush(EntitySet& entities)
{
	const auto& entityPosC = entities.posTable.at(m_entityName);

	const String name = m_entityName + U"_speak";
	entities.nameSet.insert(name);
	entities.posTable[name] = {
		Vec3{
			entityPosC.pos.x + m_offset.x,
			entityPosC.pos.y + m_offset.y,
			1.0
		}
	};
	entities.textTable[name] = {
		m_text,
		Font{ 20 },
	};
}

State::Action SpeakState::update(EntitySet& entities)
{
	if (KeySpace.down())
	{
		return Action::Pop(); // 決定キーで終了
	}
	return Action::None();
}

void SpeakState::onBeforePop(EntitySet& entities)
{
	entities.erase(m_entityName + U"_speak");
}


/*
* WalkState
*/

class WalkState : public State
{
public:
	WalkState(const TOMLValue& param);

	void onAfterPush(EntitySet& entities) override;
	Action update(EntitySet& entities) override;
	void onBeforePop(EntitySet& entities) override;

private:
	const String m_entityName;
	double m_from;
	const double m_to;
	const double m_speed;
	Timer m_timer;
};

WalkState::WalkState(const TOMLValue& param)
	: m_entityName{ param[U"entity"].getString() }
	, m_to{ param[U"to"].get<double>() }
	, m_from{ 0.0 }
	, m_speed{ param[U"speed"].get<double>() }
{
}

void WalkState::onAfterPush(EntitySet& entities)
{
	const auto& posC = entities.posTable.at(m_entityName);
	m_from = posC.pos.x;
	m_timer = Timer{
		SecondsF(Abs(m_to - m_from) / m_speed),
		StartImmediately::Yes
	};

	auto& imageC = entities.imageTable.at(m_entityName);
	if (m_to < m_from)
	{
		imageC.imagePos.x = 1; // 左向き
	}
	else if (m_from < m_to)
	{
		imageC.imagePos.x = 2; // 右向き
	}
}

State::Action WalkState::update(EntitySet& entities)
{
	auto& posC = entities.posTable.at(m_entityName);
	const double t = m_timer.progress0_1();
	posC.pos.x = (1 - t) * m_from + t * m_to;

	return m_timer.isRunning() ? Action::None() : Action::Pop();
}

void WalkState::onBeforePop(EntitySet&)
{
}


/*
* AnimState
*/

class AnimState : public State
{
public:
	AnimState(const TOMLValue& param);

	void onAfterPush(EntitySet& entities) override;
	Action update(EntitySet& entities) override;
	void onBeforePop(EntitySet& entities) override;

private:
	const String m_entityName;
	const Point m_imagePos;
	const bool m_isHidden;
};

AnimState::AnimState(const TOMLValue& param)
	: m_entityName{ param[U"entity"].getString() }
	, m_imagePos{
		param[U"imagePos.x"].get<int32>(),
		param[U"imagePos.y"].get<int32>()
	}
	, m_isHidden{ param[U"isHidden"].getOr<bool>(false) }
{
}

void AnimState::onAfterPush(EntitySet& entities)
{
	auto& imageC = entities.imageTable.at(m_entityName);
	imageC.imagePos = m_imagePos;
	imageC.isHidden = m_isHidden;
}

State::Action AnimState::update(EntitySet&)
{
	return Action::Pop();
}

void AnimState::onBeforePop(EntitySet&)
{
}


/*
* AdventureState
*/

class AdventureState : public State
{
public:
	AdventureState(const TOMLValue& param);

	void onAfterPush(EntitySet& entities) override;
	Action update(EntitySet& entities) override;
	void onBeforePop(EntitySet& entities) override;

private:
	const String m_entityName; // 操作するEntity名
	HashTable<String, String> m_link; // Entity名とシナリオ名を紐づける

};

AdventureState::AdventureState(const TOMLValue& param)
	: m_entityName(param[U"entity"].getString())
{
	// LinkComponentのようなものをEntityに持たせる方が付け外しが容易
	// 今回はStateに持たせて楽に済ませる
	param[U"link"].tableView();
	for (const auto& [name, value] : param[U"link"].tableView())
	{
		m_link[name] = value.getString();
	}
}

void AdventureState::onAfterPush(EntitySet&)
{
}

// ScenarioStateを参照するので、Adventure::updateは下で実装

void AdventureState::onBeforePop(EntitySet&)
{
}


/*
* ScenarioState
*/

class ScenarioState : public State
{
public:
	using MakeStateFunc
		= std::function<std::unique_ptr<State>(const TOMLValue&)>;

	ScenarioState(const String& scenarioName);
	ScenarioState(const TOMLValue& param);

	void onAfterPush(EntitySet& entities) override;
	Action update(EntitySet& entities) override;
	void onBeforePop(EntitySet& entities) override;

private:
	void makeEntities(EntitySet& entities, const TOMLValue& params);

	template<typename Type>
	MakeStateFunc makeStateFunc()
	{
		return [](const TOMLValue& param) {
			return std::make_unique<Type>(param);
		};
	}

	// シナリオ管理
	TOMLTableArrayIterator m_now;
	TOMLTableArrayIterator m_end;

	// ここで作ったEntityの名前（pop時に削除する用）
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

State::Action ScenarioState::update(EntitySet& entities)
{
	// 最後まで読んだら pop
	if (m_now == m_end) { return Action::Pop(); }

	static const HashTable<String, MakeStateFunc> MAKE_TABLE = {
		{ U"wait", makeStateFunc<WaitState>() },
		{ U"speak", makeStateFunc<SpeakState>() },
		{ U"walk", makeStateFunc<WalkState>() },
		{ U"anim", makeStateFunc<AnimState>() },
		{ U"adventure", makeStateFunc<AdventureState>() },
		{ U"scenario", makeStateFunc<ScenarioState>() },
	};

	TOMLValue nowToml = *m_now;
	++m_now;

	if (nowToml[U"make"].isTableArray())
	{
		// Entity作成
		makeEntities(entities, nowToml[U"make"]);
		return Action::None();
	}

	if (nowToml[U"push"].isString())
	{
		// push
		const String stateName = nowToml[U"push"].getString();
		return Action::Push(MAKE_TABLE.at(stateName)(nowToml[U"param"]));
	}

	if (nowToml[U"replace"].isString())
	{
		// replace
		const String stateName = nowToml[U"replace"].getString();
		return Action::Replace(MAKE_TABLE.at(stateName)(nowToml[U"param"]));
	}

	return Action::None();
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

// AdventureState::updateの実装
State::Action AdventureState::update(EntitySet& entities)
{
	auto& posC = entities.posTable.at(m_entityName);
	auto& imageC = entities.imageTable.at(m_entityName);
	if (KeyLeft.pressed())
	{
		posC.pos.x -= 100.0 * Scene::DeltaTime();
		imageC.imagePos.x = 1;
	}
	else if (KeyRight.pressed())
	{
		posC.pos.x += 100.0 * Scene::DeltaTime();
		imageC.imagePos.x = 2;
	}
	posC.pos.x = Clamp(posC.pos.x, 0.0, 640.0);


	for (const auto& [targetName, scenarioName] : m_link)
	{
		const auto& targetPosC = entities.posTable.at(targetName);
		if (Abs(posC.pos.x - targetPosC.pos.x) < 60.0 && KeySpace.down())
		{
			return Action::Push(
				std::make_unique<ScenarioState>(scenarioName)
			);
		}
	}

	return Action::None();
}


/*
* StateStack
*/

class StateStack
{
public:
	StateStack();
	void update(EntitySet& entities);

private:
	void pop(EntitySet& entities);
	void push(EntitySet& entities, std::unique_ptr<State>&& nextState);

	// top以外のデータも見たいのでArrayで実装
	// 末尾以外のデータを編集しないように気を付ける
	Array<std::unique_ptr<State>> m_stack;
};

StateStack::StateStack()
{
	m_stack.push_back(std::make_unique<ScenarioState>(U"init"));
}

void StateStack::update(EntitySet& entities)
{
	if (m_stack.empty()) { return; }

	// Stateの更新して、スタック操作を取得
	auto [type, nextState] = m_stack.back()->update(entities);

	switch (type)
	{
	case State::Action::Type::NONE:
		break;

	case State::Action::Type::POP:
		pop(entities);
		break;

	case State::Action::Type::PUSH:
		push(entities, std::move(nextState));
		break;

	case State::Action::Type::REPLACE:
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
* 描画
*/

// Enity1つ描画
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

// Entityまとめて描画
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


/*
* Main
*/

void Main()
{
	Window::Resize(Size{ 640, 480 });
	Scene::SetBackground(Color{ 0x0f });

	EntitySet entities;
	StateStack stateStack;

	while (System::Update())
	{
		stateStack.update(entities);
		drawEntities(entities);

		Cursor::RequestStyle(CursorStyle::Hidden);
	}
}
