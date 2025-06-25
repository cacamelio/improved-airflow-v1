#include <algorithm>

#include "../tools/memory/memory.h"

#include "../other/game_functions.h"

#include "c_animstate.h"
#include "entity.h"

void c_animstate::create(c_csplayer* player)
{
	func_ptrs::create_animstate(this, player);
}

void c_animstate::update(const vector3d& angle)
{
	func_ptrs::update_animstate(this, NULL, NULL, angle.y, angle.x, NULL);
}

void c_animstate::reset()
{
	func_ptrs::reset_animstate(this);
}

float_t c_animstate::get_yaw_modifer()
{
	//CCSGOPlayerAnimState::SetupVelocity
	// ref: https://prnt.sc/26jlokg

	// improve me:
	/*
	 CCSGOPlayerAnimState::Update;
	 ref: https://prnt.sc/26jlbf6
	*/

	auto animstate = this;

	if (!animstate)
		return 0.0f;

	float_t v38 = animstate->speed_as_portion_of_walk_top_speed;
	float_t v39, v40, v41, v42, v43, v44;

	if (v38 >= 0.0)
		v39 = fminf(v38, 1.0);
	else
		v39 = 0.0;
	v40 = ((animstate->walk_run_transition * -0.30000001) - 0.19999999) * v39;
	v41 = animstate->anim_duck_amount;
	v42 = v40 + 1.0;

	if (v41 > 0.0)
	{
		v43 = animstate->speed_as_portion_of_crouch_top_speed;
		if (v43 >= 0.0)
			v44 = fminf(v43, 1.0);
		else
			v44 = 0.0;
		v42 = v42 + ((v41 * v44) * (0.5 - v42));
	}

	return v42;
}

float c_animstate::get_min_rotation()
{
	return aim_yaw_min * get_yaw_modifer();
}

float c_animstate::get_max_rotation()
{
	return aim_yaw_max * get_yaw_modifer();
}

void c_animstate::increment_layer_cycle(c_animation_layers* layer, bool loop)
{
	float new_cycle = (layer->playback_rate * last_update_increment) + layer->cycle;
	if (!loop && new_cycle >= 1.0f)
		new_cycle = 0.999f;

	new_cycle -= (int32_t)(new_cycle);
	if (new_cycle < 0.0f)
		new_cycle += 1.0f;

	if (new_cycle > 1.0f)
		new_cycle -= 1.0f;

	layer->cycle = new_cycle;
}

bool c_animstate::is_layer_sequence_finished(c_animation_layers* layer, float time)
{
	return (layer->playback_rate * time) + layer->cycle >= 1.0f;
}

void c_animstate::set_layer_cycle(c_animation_layers* layer, float_t cycle)
{
	if (layer)
		layer->cycle = cycle;
}

void c_animstate::set_layer_rate(c_animation_layers* layer, float rate)
{
	if (layer)
		layer->playback_rate = rate;
}

void c_animstate::set_layer_weight(c_animation_layers* layer, float weight)
{
	if (layer)
		layer->weight = weight;
}

void c_animstate::set_layer_weight_rate(c_animation_layers* layer, float prev)
{
	if (layer)
		layer->weight_delta_rate = (layer->weight - prev) / last_update_increment;
}

void c_animstate::set_layer_sequence(c_animation_layers* layer, int sequence, int index)
{
	if (sequence <= 1)
		return;

	auto cs_player = reinterpret_cast<c_csplayer*>(player);
	if (cs_player && layer->sequence != sequence)
		cs_player->invalidate_physics_recursive(16);

	layer->sequence = sequence;
	layer->playback_rate = cs_player->get_layer_sequence_cycle_rate(layer, sequence);

	if (cs_player && layer->cycle != 0.f)
		cs_player->invalidate_physics_recursive(8);

	layer->cycle = 0.f;

	if (cs_player && layer->weight != 0.f)
		cs_player->invalidate_physics_recursive(16);

	layer->weight = 0.f;

	static auto update_layer_order_preset = patterns::update_layer_order_preset.as<void(__thiscall*)(void*, int, int)>();
	update_layer_order_preset(this, index, sequence);
}

int c_animstate::select_sequence_from_activity_modifier(int iActivity)
{
	bool ducking = anim_duck_amount > 0.55f;
	bool running = speed_as_portion_of_walk_top_speed > 0.25f;

	int current_sequence = 0;
	switch (iActivity)
	{
	case act_csgo_jump:
	{
		current_sequence = 15 + static_cast<int32_t>(running);
		if (ducking)
			current_sequence = 17 + static_cast<int32_t>(running);
	}
	break;

	case act_csgo_alive_loop:
	{
		current_sequence = 8;
		if (weapon_last != weapon)
			current_sequence = 9;
	}
	break;

	case act_csgo_idle_adjust_stoppedmoving:
	{
		current_sequence = 6;
	}
	break;

	case act_csgo_fall:
	{
		current_sequence = 14;
	}
	break;

	case act_csgo_idle_turn_balanceadjust:
	{
		current_sequence = 4;
	}
	break;

	case act_csgo_land_light:
	{
		current_sequence = 20;
		if (running)
			current_sequence = 22;

		if (ducking)
		{
			current_sequence = 21;
			if (running)
				current_sequence = 19;
		}
	}
	break;

	case act_csgo_land_heavy:
	{
		current_sequence = 23;
		if (running)
			current_sequence = 24;
	}
	break;

	case act_csgo_climb_ladder:
	{
		current_sequence = 13;
	}
	break;

	default:
		break;
	}

	return current_sequence;
}

float c_animstate::get_layer_ideal_weight_from_seq_cycle(c_animation_layers* layer)
{
	auto hdr = ((c_csplayer*)player)->get_studio_hdr();
	if (!hdr)
		return 0;

	auto sequence_desc = hdr->get_sequence_desc(layer->sequence);
	if (!sequence_desc)
		return 0;

	float cycle = layer->cycle;
	if (cycle >= 0.999f)
		cycle = 1.f;

	float ease_in = *(float*)((std::uintptr_t)sequence_desc + 0x68);
	float ease_out = *(float*)((std::uintptr_t)sequence_desc + 0x6C);

	float ideal_weight = 0.f;
	if (ease_in > 0 && cycle < ease_in)
		ideal_weight = math::smoothstep_bounds(0, ease_in, cycle);
	else if (ease_out < 1 && cycle > ease_out)
		ideal_weight = math::smoothstep_bounds(1.0f, ease_out, cycle);

	if (ideal_weight < 0.0015f)
		return 0.f;

	return (std::clamp(ideal_weight, 0.f, 1.f));
}

void c_animstate::update_layer(c_animation_layers* layer, int sequence, float rate, float cycle, float weight, int index)
{
	static auto update_layer_order_preset = patterns::update_layer_order_preset.as<void(__thiscall*)(void*, int, int)>();

	if (sequence > 1)
	{
		auto p = (c_csplayer*)player;

		if (layer->sequence != sequence)
			p->invalidate_physics_recursive(16);

		layer->sequence = sequence;
		layer->playback_rate = rate;

		float temp_weight = std::clamp(weight, 0.f, 1.f);
		float temp_cycle = std::clamp(cycle, 0.f, 1.f);

		if (layer->cycle != temp_cycle)
			p->invalidate_physics_recursive(8);

		layer->cycle = temp_cycle;

		float prev_weight = layer->weight;
		if (prev_weight != temp_weight && (prev_weight == 0.f || temp_weight == 0.f))
			p->invalidate_physics_recursive(16);

		layer->weight = temp_weight;

		update_layer_order_preset(this, index, layer->sequence);
	}
}