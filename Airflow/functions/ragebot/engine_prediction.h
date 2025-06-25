#pragma once
#include "../../base/sdk.h"

#include "../../base/tools/math.h"

#include "../../base/other/game_functions.h"

#include "../../base/sdk/c_usercmd.h"
#include "../../base/sdk/entity.h"

#include <memory>
#include <optional>
#undef local

class c_engine_prediction_restore
{
private:
	bool in_prediciton{};
	bool first_time_predicted{};

	int random_seed{};
	int player{};

	vector3d old_origin{};
	c_movedata movedata{};

	c_usercmd* cmd_ptr{};
	c_usercmd pred_cmd{};

public:
	inline c_engine_prediction_restore()
	{
		in_prediciton = interfaces::prediction->in_prediction;
		first_time_predicted = interfaces::prediction->is_first_time_predicted;

		random_seed = **patterns::prediction_random_seed.as< int** >();
		player = **patterns::prediction_player.as< int** >();

		movedata = **(c_movedata**)((uintptr_t)interfaces::game_movement + 8);

		cmd_ptr = *(c_usercmd**)((uintptr_t)g_ctx.local + 0x3348);
		pred_cmd = *(c_usercmd*)((uintptr_t)g_ctx.local + 0x3298);

		old_origin = *(vector3d*)((uintptr_t)g_ctx.local + 0x3AC);
	}

	inline ~c_engine_prediction_restore()
	{
		interfaces::prediction->in_prediction = std::move(in_prediciton);
		interfaces::prediction->is_first_time_predicted = std::move(first_time_predicted);

		**patterns::prediction_random_seed.as< int** >() = std::move(random_seed);
		**patterns::prediction_player.as< int** >() = std::move(player);

		**(c_movedata**)((uintptr_t)interfaces::game_movement + 8) = std::move(movedata);

		*(c_usercmd**)((uintptr_t)g_ctx.local + 0x3348) = cmd_ptr;
		*(c_usercmd*)((uintptr_t)g_ctx.local + 0x3298) = std::move(pred_cmd);

		*(vector3d*)((uintptr_t)g_ctx.local + 0x3AC) = std::move(old_origin);
	}
};

class c_engine_prediction
{
public:
	struct net_data_t
	{
		int cmd_number{};
		int tickbase{};

		float vel_modifier{};
		float fall_velocity{};
		float duck_amt{};
		float duck_speed{};
		float thirdperson_recoil{};

		vector3d punch{};
		vector3d punch_vel{};
		vector3d view_offset{};
		vector3d view_punch{};
		vector3d velocity{};
		vector3d network_origin{};

		bool filled{};

		__forceinline void reset()
		{
			cmd_number = 0;

			vel_modifier = 0.f;
			fall_velocity = 0.f;
			duck_amt = 0.f;
			duck_speed = 0.f;
			thirdperson_recoil = 0.f;

			punch.reset();
			punch_vel.reset();
			view_offset.reset();
			view_punch.reset();
			velocity.reset();
			network_origin.reset();

			filled = false;
		}
	};

	struct netvars_t
	{
		bool done{ false };
		bool walking{};
		bool scoped{};

		int flags{};

		int tickbase{};
		int move_state{};
		int move_type{};

		float recoil_index{};
		float acc_penalty{};
		float inaccuracy{};
		float spread{};
		float lby{};
		float thirdperson_recoil{};
		float duck_amount{};

		vector3d origin{};
		vector3d abs_origin{};
		vector3d viewoffset{};
		vector3d aimpunch{};
		vector3d aimpunch_vel{};
		vector3d viewpunch{};
		vector3d velocity{};
		vector3d ladder_normal{};

		uint32_t ground_entity{};

		__forceinline void fill()
		{
			walking = g_ctx.local->is_walking();
			scoped = g_ctx.local->is_scoped();

			flags = g_ctx.local->flags();
			tickbase = g_ctx.local->tickbase();

			recoil_index = g_ctx.weapon->recoil_index();
			acc_penalty = g_ctx.weapon->accuracy_penalty();
			inaccuracy = g_ctx.weapon->get_inaccuracy();
			spread = g_ctx.weapon->get_spread();

			lby = g_ctx.local->lby();
			thirdperson_recoil = g_ctx.local->thirdperson_recoil();
			duck_amount = g_ctx.local->duck_amount();

			move_state = g_ctx.local->move_state();
			move_type = g_ctx.local->move_type();

			origin = g_ctx.local->origin();
			abs_origin = g_ctx.local->get_abs_origin();
			viewoffset = g_ctx.local->view_offset();
			aimpunch = g_ctx.local->aim_punch_angle();
			aimpunch_vel = g_ctx.local->aim_punch_angle_vel();
			viewpunch = g_ctx.local->view_punch_angle();
			velocity = g_ctx.local->velocity();
			ladder_normal = g_ctx.local->ladder_normal();
			ground_entity = g_ctx.local->ground_entity();

			done = true;
		}

		__forceinline void set(bool set_ground_entity = false)
		{
			if (!done)
				return;

			g_ctx.weapon->recoil_index() = recoil_index;
			g_ctx.weapon->accuracy_penalty() = acc_penalty;

			g_ctx.local->origin() = origin;
			g_ctx.local->set_abs_origin(abs_origin);
			g_ctx.local->view_offset() = viewoffset;
			g_ctx.local->aim_punch_angle() = aimpunch;
			g_ctx.local->aim_punch_angle_vel() = aimpunch_vel;
			g_ctx.local->view_punch_angle() = viewpunch;

			if (set_ground_entity)
			{
				g_ctx.local->ground_entity() = ground_entity;
				g_ctx.local->flags() = flags;
			}
		}

		void set_for_animations()
		{
			if (!done)
				return;

			g_ctx.local->duck_amount() = duck_amount;
			g_ctx.weapon->recoil_index() = recoil_index;
			g_ctx.weapon->accuracy_penalty() = acc_penalty;

			g_ctx.local->origin() = origin;
			g_ctx.local->set_abs_origin(origin);
			g_ctx.local->view_offset() = viewoffset;
			g_ctx.local->aim_punch_angle() = aimpunch;
			g_ctx.local->aim_punch_angle_vel() = aimpunch_vel;
			g_ctx.local->view_punch_angle() = viewpunch;

			g_ctx.local->is_walking() = walking;
			g_ctx.local->is_scoped() = scoped;

			g_ctx.local->lby() = lby;
			g_ctx.local->thirdperson_recoil() = thirdperson_recoil;

			g_ctx.local->move_state() = move_state;
			g_ctx.local->move_type() = move_type;

			g_ctx.local->ground_entity() = ground_entity;
			g_ctx.local->flags() = flags;

			g_ctx.local->abs_velocity() = g_ctx.local->velocity() = velocity;
			g_ctx.local->ladder_normal() = ladder_normal;
		}

		inline void reset()
		{
			recoil_index = 0.f;
			acc_penalty = 0.f;
			inaccuracy = 0.f;
			spread = 0.f;
			flags = 0;
			tickbase = 0;

			origin.reset();
			abs_origin.reset();
			viewoffset.reset();
			aimpunch.reset();
			aimpunch_vel.reset();
			viewpunch.reset();

			ground_entity = 0;

			done = false;
		}
	};

	bool reset_net_data{};
	bool old_in_prediction{};
	bool old_first_time_predicted{};

	int old_tick_base{};
	int old_tick_count{};

	float old_cur_time{};
	float old_frame_time{};

	float old_recoil_index{};
	float old_accuracy_penalty{};

	uint32_t old_seed{};

	c_usercmd* old_cmd{};
	c_movedata move_data{};

	std::array< net_data_t, 150 > net_data{};

	__forceinline void reset()
	{
		if (!reset_net_data)
			return;

		for (auto& d : net_data)
			d.reset();

		reset_net_data = false;
	}

	vector3d unprediced_velocity{};
	int unpredicted_flags{};
	int predicted_buttons{};

	float predicted_inaccuracy{};
	float predicted_spread{};

	int old_pred_tick{};
	float interp_amount{};

	int* prediction_player{};
	int* prediction_random_seed{};

	netvars_t unpred_vars[150]{};
	netvars_t predicted_vars[150]{};

	inline netvars_t* get_unpredicted_vars(int cmd)
	{
		return &unpred_vars[cmd % 150];
	}

	inline netvars_t* get_predicted_vars(int cmd)
	{
		return &predicted_vars[cmd % 150];
	}

	inline void on_local_death()
	{
		this->unprediced_velocity.reset();
		this->unpredicted_flags = 0;
		this->predicted_buttons = 0;

		this->predicted_inaccuracy = 0.f;
		this->predicted_spread = 0.f;

		this->old_pred_tick = 0;
		this->interp_amount = 0.f;
		
		this->reset_net_data = false;
		this->old_in_prediction = false;
		this->old_first_time_predicted = false;

		this->old_tick_base = 0;
		this->old_tick_count = 0;

		this->old_cur_time = 0.f;
		this->old_frame_time = 0.f;

		this->old_recoil_index = 0.f;
		this->old_accuracy_penalty = 0.f;

		this->old_seed = 0;

		this->old_cmd = nullptr;

		for (auto& i : unpred_vars)
			i.reset();

		for (auto& i : predicted_vars)
			i.reset();

		this->reset();

		memset(&this->move_data, 0, sizeof(c_movedata));
	}

	void net_compress_store(int tick);
	void net_compress_apply(int tick);

	void init();
	void update();

	void start(c_csplayer* local, c_usercmd* cmd);
	vector3d get_eye_pos(const vector3d& angle);
	void repredict(c_csplayer* local, c_usercmd* cmd, bool real_cmd = false);
	void finish(c_csplayer* local);
};