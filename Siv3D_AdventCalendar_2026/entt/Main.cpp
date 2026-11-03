#include <Siv3D.hpp>
#include "entt.hpp"

/*
* 座標
*/

// 座標
struct Position {
	Vec3 pos; // {x, y}: 画面上の座標、z: 奥行き
};

/*
* 描画
*/

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

struct Hidden {
	// このコンポーネントを持つとき非表示にする
};

namespace DrawSystem {

	void DrawOne(const ImageDrawable& d, const Vec2& pos) {
		d.texture(d.imagePos * d.imageSize, d.imageSize).drawAt(pos);
	}

	void DrawOne(const TextDrawable& d, const Vec2& pos) {
		d.font(d.text).drawAt(pos, Palette::Black);
	}

	void Draw(const entt::registry& registry) {
		Array<std::pair<double, entt::entity>> drawList;
		for (auto [entity, position, drawable] : registry.view<Position, Drawable>(entt::exclude<Hidden>).each()) {
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

/*
* 名前
*/

struct Name {
	const String value; // 後から変更されるとLookupと不整合を起こすので const
};

using NameLookup = HashTable<String, entt::entity>;

void NameInit(entt::registry& registry) {
	registry.ctx().emplace<NameLookup>();

	registry.on_construct<Name>().connect<[](entt::registry& registry, entt::entity entity) {
		// 重複チェックは今回スキップ
		registry.ctx().get<NameLookup>()[registry.get<Name>(entity).value] = entity;
	}>();
	registry.on_destroy<Name>().connect<[](entt::registry& registry, entt::entity entity) {
		registry.ctx().get<NameLookup>().erase(registry.get<Name>(entity).value);
	}>();
}

/*
* State
*/

class State {
public:
	struct Action {
		enum class Type {
			NONE,
			POP,
			PUSH,
			RESET, // clear + push
		};

		Type type;
		std::unique_ptr<State> nextState;

		static Action None() { return { Type::NONE, nullptr }; }
		static Action Pop() { return{ Type::POP, nullptr }; }
		static Action Push(std::unique_ptr<State>&& state) { return{ Type::PUSH, std::move(state) }; }
		static Action Reset(std::unique_ptr<State>&& state) { return{ Type::RESET, std::move(state) }; }
	};

	virtual ~State() = default;

	virtual void onAfterPush(entt::registry& registry) = 0;
	virtual Action update(entt::registry& registry) = 0;
	virtual void onBeforePop(entt::registry& registry) = 0;

	virtual String getName() const = 0;
};


/*
* WaitState
*/

class WaitState : public State {
public:
	WaitState(const TOMLValue& param);

	void onAfterPush(entt::registry& registry) override;
	Action update(entt::registry& registry) override;
	void onBeforePop(entt::registry& registry) override;

	String getName() const override {
		return U"WaitState";
	}

private:
	Timer m_timer;
};

WaitState::WaitState(const TOMLValue& param)
	: m_timer{ SecondsF(param.get<double>()), StartImmediately::Yes }
{
}

void WaitState::onAfterPush(entt::registry&) {
}

State::Action WaitState::update(entt::registry&) {
	return m_timer.isRunning() ? Action::None() : Action::Pop();
}

void WaitState::onBeforePop(entt::registry&) {
}


/*
* SpeakState
*/

class SpeakState : public State {
public:
	SpeakState(const TOMLValue& param);

	void onAfterPush(entt::registry& registry) override;
	Action update(entt::registry& registry) override;
	void onBeforePop(entt::registry& registry) override;

	String getName() const override {
		return U"SpeakState";
	}

private:
	entt::entity m_entity = entt::null;
	const String m_speakerName;
	const String m_text;
	const Vec2 m_offset;
};

SpeakState::SpeakState(const TOMLValue& param)
	: m_speakerName{ param[U"name"].getString() }
	, m_text{ param[U"text"].getString() }
	, m_offset{
		param[U"offset.x"].getOr<double>(0.0),
		param[U"offset.y"].getOr<double>(0.0)
	}
{
}

void SpeakState::onAfterPush(entt::registry& registry) {
	const entt::entity speakerEntity = registry.ctx().get<NameLookup>().at(m_speakerName);
	const Vec3 pos = registry.get<Position>(speakerEntity).pos;

	m_entity = registry.create();
	registry.emplace<Position>(m_entity, Vec3{
		pos.x + m_offset.x,
		pos.y + m_offset.y,
		1.0,
	});
	registry.emplace<Drawable>(m_entity, TextDrawable{
		.font = Font{ 20 },
		.text = m_text,
	});
}

State::Action SpeakState::update(entt::registry&) {
	if (KeySpace.down()) {
		return Action::Pop(); // 決定キーで終了
	}
	return Action::None();
}

void SpeakState::onBeforePop(entt::registry& registry) {
	registry.destroy(m_entity);
}


/*
* WalkState
*/

class WalkState : public State
{
public:
	WalkState(const TOMLValue& param);

	void onAfterPush(entt::registry& registry) override;
	Action update(entt::registry& registry) override;
	void onBeforePop(entt::registry& registry) override;

	String getName() const override {
		return U"WalkState";
	}

private:
	const String m_name;
	entt::entity m_entity = entt::null;
	const double m_to;
	const double m_speed;
};

WalkState::WalkState(const TOMLValue& param)
	: m_name{ param[U"name"].getString() }
	, m_to{ param[U"to"].get<double>() }
	, m_speed{ param[U"speed"].get<double>() }
{
}

void WalkState::onAfterPush(entt::registry& registry) {
	m_entity = registry.ctx().get<NameLookup>().at(m_name);

	const auto& position = registry.get<Position>(m_entity);

	auto& drawable = registry.get<Drawable>(m_entity);
	auto& image = std::get<ImageDrawable>(drawable);

	if (m_to < position.pos.x) {
		image.imagePos.x = 1; // 左向き
	}
	else if (m_to > position.pos.x) {
		image.imagePos.x = 2; // 右向き
	}
}

State::Action WalkState::update(entt::registry& registry) {
	auto& pos = registry.get<Position>(m_entity).pos;

	const int32 direction = Math::Sign(m_to - pos.x);
	if (direction == 0) {
		return Action::Pop();
	}

	pos.x += direction * m_speed * Scene::DeltaTime();
	if (Math::Sign(m_to - pos.x) != direction) {
		pos.x = m_to;
		return Action::Pop();
	}
	return Action::None();
}

void WalkState::onBeforePop(entt::registry&) {
}


/*
* AnimState
*/

class AnimState : public State
{
public:
	AnimState(const TOMLValue& param);

	void onAfterPush(entt::registry& registry) override;
	Action update(entt::registry& registry) override;
	void onBeforePop(entt::registry& registry) override;

	String getName() const override {
		return U"AnimState";
	}

private:
	const String m_name;
	const Point m_imagePos;
	const bool m_isHidden;
};

AnimState::AnimState(const TOMLValue& param)
	: m_name{ param[U"name"].getString() }
	, m_imagePos{
		param[U"imagePos.x"].get<int32>(),
		param[U"imagePos.y"].get<int32>()
	}
	, m_isHidden{ param[U"isHidden"].getOr<bool>(false) }
{
}

void AnimState::onAfterPush(entt::registry& registry) {
	const entt::entity entity = registry.ctx().get<NameLookup>().at(m_name);

	auto& drawable = registry.get<Drawable>(entity);
	auto& image = std::get<ImageDrawable>(drawable);
	image.imagePos = m_imagePos;

	if (m_isHidden) {
		registry.emplace_or_replace<Hidden>(entity);
	}
	else {
		registry.remove<Hidden>(entity);
	}
}

State::Action AnimState::update(entt::registry&)
{
	return Action::Pop();
}

void AnimState::onBeforePop(entt::registry&)
{
}


/*
* AdventureState
*/

struct ScenarioLink {
	String scenario; // 調べられたときにpushするシナリオ名
};

class AdventureState : public State {
public:
	AdventureState(const TOMLValue& param);

	void onAfterPush(entt::registry& registry) override;
	Action update(entt::registry& registry) override;
	void onBeforePop(entt::registry& registry) override;

	String getName() const override {
		return U"AdventureState";
	}

private:
	const String m_name; // 操作するEntity名
	entt::entity m_entity = entt::null;
};

AdventureState::AdventureState(const TOMLValue& param)
	: m_name(param[U"name"].getString())
{
}

void AdventureState::onAfterPush(entt::registry& registry) {
	m_entity = registry.ctx().get<NameLookup>().at(m_name);
}

// ScenarioStateを参照するので、Adventure::updateは下で実装

void AdventureState::onBeforePop(entt::registry&) {
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

	void onAfterPush(entt::registry& registry) override;
	Action update(entt::registry& registry) override;
	void onBeforePop(entt::registry& registry) override;

	String getName() const override {
		return U"ScenarioState[[" + m_scenarioName + U"]]";
	}

private:
	void makeEntities(entt::registry& registry, const TOMLValue& params);

	template<typename Type>
	MakeStateFunc makeStateFunc() {
		return [](const TOMLValue& param) {
			return std::make_unique<Type>(param);
			};
	}

	// シナリオ管理
	TOMLTableArrayIterator m_now;
	TOMLTableArrayIterator m_end;

	// ここで作ったEntity（pop時に削除する用）
	HashSet<entt::entity> m_entitySetMadeOnThis;

	const String m_scenarioName = U"";
};

ScenarioState::ScenarioState(const String& scenarioName)
	: m_scenarioName{ scenarioName }
{
	static const TOMLReader reader{ U"scenario.toml" };
	m_now = reader[scenarioName].tableArrayView().begin();
	m_end = reader[scenarioName].tableArrayView().end();
}

ScenarioState::ScenarioState(const TOMLValue& param)
	: ScenarioState{ param.getString() }
{
}

void ScenarioState::onAfterPush(entt::registry& registry)
{
}

State::Action ScenarioState::update(entt::registry& registry)
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

	const auto& nowToml = *m_now;
	++m_now;

	if (nowToml[U"make"].isTableArray()) {
		// Entity作成
		makeEntities(registry, nowToml[U"make"]);
		return Action::None();
	}

	if (nowToml[U"push"].isString()) {
		// push
		const String stateName = nowToml[U"push"].getString();
		return Action::Push(MAKE_TABLE.at(stateName)(nowToml[U"param"]));
	}

	if (nowToml[U"reset"].isString()) {
		// reset
		const String stateName = nowToml[U"reset"].getString();
		return Action::Reset(MAKE_TABLE.at(stateName)(nowToml[U"param"]));
	}

	return Action::None();
}

void ScenarioState::onBeforePop(entt::registry& registry)
{
	for (const auto& entity : m_entitySetMadeOnThis) {
		registry.destroy(entity);
	}
}

void ScenarioState::makeEntities(entt::registry& registry, const TOMLValue& params) {
	for (const auto& param : params.tableArrayView()) {
		const entt::entity entity = registry.create();
		m_entitySetMadeOnThis.insert(entity);

		if (param[U"name"].isString()) {
			registry.emplace<Name>(entity, param[U"name"].getString());
		}

		if (param[U"pos"].isTable()) {
			TOMLValue pos = param[U"pos"];
			registry.emplace<Position>(entity, Vec3{
			  pos[U"x"].get<double>(),
			  pos[U"y"].get<double>(),
			  pos[U"z"].get<double>(),
			});
		}

		if (param[U"image"].isTable()) {
			TOMLValue image = param[U"image"];
			registry.emplace<Drawable>(entity, ImageDrawable{
			  .texture = Texture{ image[U"path"].getString() },
			  .imageSize = Size{
				image[U"size"][U"x"].get<int32>(),
				image[U"size"][U"y"].get<int32>(),
			  },
			  .imagePos = Point{
				image[U"pos"][U"x"].get<int32>(),
				image[U"pos"][U"y"].get<int32>(),
			  }
			});
		}
		else if (param[U"text"].isTable()) {
			TOMLValue text = param[U"text"];
			registry.emplace<Drawable>(entity, TextDrawable{
			  .font = Font{ text[U"font"][U"size"].get<int32>(), },
			  .text = text[U"text"].getString(),
			});
		}

		if (param[U"link"].isString()) {
			registry.emplace<ScenarioLink>(entity, param[U"link"].getString());
		}
	}
}

// AdventureState::updateの実装
State::Action AdventureState::update(entt::registry& registry)
{
	Vec3& pos = registry.get<Position>(m_entity).pos;
	auto& drawable = registry.get<Drawable>(m_entity);
	auto& image = std::get<ImageDrawable>(drawable);

	if (KeyLeft.pressed())
	{
		pos.x -= 100.0 * Scene::DeltaTime();
		image.imagePos.x = 1;
	}
	else if (KeyRight.pressed())
	{
		pos.x += 100.0 * Scene::DeltaTime();
		image.imagePos.x = 2;
	}
	pos.x = Clamp(pos.x, 0.0, 640.0);

	for (const auto& [_, targetPos, link] : registry.view<Position, ScenarioLink>().each()) {
		if (Abs(pos.x - targetPos.pos.x) < 60.0 && KeySpace.down()) {
			return Action::Push(
				std::make_unique<ScenarioState>(link.scenario)
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
	void update(entt::registry& registry);

private:
	void pop(entt::registry& registry);
	void push(entt::registry& registry, std::unique_ptr<State>&& nextState);

	// top以外のデータも見たいのでArrayで実装
	// 末尾以外のデータを編集しないように気を付ける
	Array<std::unique_ptr<State>> m_stack;
};

StateStack::StateStack()
{
	m_stack.push_back(std::make_unique<ScenarioState>(U"init"));
	Print << U"┏ScenarioState[[init]]";
}

void StateStack::update(entt::registry& registry)
{
	if (m_stack.empty()) { return; }

	// Stateの更新して、スタック操作を取得
	auto [type, nextState] = m_stack.back()->update(registry);

	switch (type)
	{
	case State::Action::Type::NONE:
		break;

	case State::Action::Type::POP:
		pop(registry);
		break;

	case State::Action::Type::PUSH:
		push(registry, std::move(nextState));
		break;

	case State::Action::Type::RESET:
		while (not m_stack.empty()) { pop(registry); }
		push(registry, std::move(nextState));
		break;
	}
}

void StateStack::pop(entt::registry& registry)
{
	m_stack.back()->onBeforePop(registry);
	m_stack.pop_back();

	String debug;
	for (int32 _ = 0; _ < m_stack.size(); ++_)
	{
		debug += U"┃";
	}
	Print << debug << U"┗";
}

void StateStack::push(entt::registry& registry, std::unique_ptr<State>&& nextState)
{
	String debug;
	for (int32 _ = 0; _ < m_stack.size(); ++_)
	{
		debug += U"┃";
	}
	m_stack.push_back(std::move(nextState));
	m_stack.back()->onAfterPush(registry);
	Print << debug + U"┏" + m_stack.back()->getName();
}


/*
* Main
*/

void Main()
{
	Window::Resize(Size{ 640, 480 });
	Scene::SetBackground(Color{ 0x0f });

	entt::registry registry;
	NameInit(registry);

	StateStack stateStack;

	while (System::Update())
	{
		stateStack.update(registry);
		DrawSystem::Draw(registry);

		Cursor::RequestStyle(CursorStyle::Hidden);
	}
}
