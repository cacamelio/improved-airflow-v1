#include "animfix.h"
#include "../features.h"
#include "../../base/sdk/c_csplayerresource.h"
#include "../../base/tools/threads.h"

float land_time = 0.0f;
auto land_in_cycle = false;
auto is_landed = false;
auto on_ground = false;

void c_animation_fix::anim_player_t::adjust_roll_angle(records_t* record)
{
	auto& info = resolver_info[ptr->index()];

	if (g_cfg.binds[force_roll_b].toggled)
	{
		if (info.side != side_zero && info.side != side_original)
			record->eye_angles.z = g_cfg.rage.roll_amt * -info.side;
		else
			record->eye_angles.z = g_cfg.rage.roll_amt;

		if (!record->shooting)
		{
			record->eye_angles.x += g_cfg.rage.roll_amt_pitch;
			record->eye_angles.x = std::min(120.f, record->eye_angles.x);
		}
	}


	if (last_record) {
		if (record->fix_jitter_angle)
		{
			record->eye_angles.y = math::normalize(last_record->eye_angles.y);

			record->fix_jitter_angle = false;

		}
	}


}

inline simulated_data_t* get_data_by_index(records_t* record, int side)
{
	switch (side)
	{
	case side_left:
		return &record->sim_left;
	case side_right:
		return &record->sim_right;
	case side_zero:
		return &record->sim_zero;
	case side_original:
		return &record->sim_orig;
	}
}

void c_animation_fix::sim_step_update(c_csplayer* player) //update the fakelag simulation by one tick
{
	const auto backup_frametime = interfaces::global_vars->frame_time;
	const auto backup_curtime = interfaces::global_vars->cur_time;
	const auto old_flags = player->flags();

	// get player anim state
	auto state = player->animstate();

	// allow re-animate player in this tick
	if (state->last_update_frame == interfaces::global_vars->frame_time)
		state->last_update_frame -= 1.f;

	if (state->last_update_time == interfaces::global_vars->cur_time)
		state->last_update_time -= 1.f;

	// fixes for networked players
	interfaces::global_vars->frame_time = interfaces::global_vars->interval_per_tick;
	interfaces::global_vars->cur_time = player->simulation_time();
	player->e_flags() &= ~(EFL_DIRTY_ABSVELOCITY | EFL_DIRTY_ABSTRANSFORM);

	// notify the other hooks to instruct animations and pvs fix
	player->update_clientside_animation();

	player->invalidate_physics_recursive(0x8);

	// restore globals
	interfaces::global_vars->cur_time = backup_curtime;
	interfaces::global_vars->frame_time = backup_frametime;
}

void c_animation_fix::anim_player_t::simulate_animation_side(records_t* record, int side)
{
	auto state = ptr->animstate();
	if (!state)
		return;

	ptr->update_weapon_dispatch_layers();

	backup_globals(cur_time);
	backup_globals(frame_time);

	if (!last_record || record->dormant)
	{
		auto& layers = record->sim_orig.layers;

		auto last_update_time = state->last_update_time;

		// this happens only when first record arrived or when enemy is out from dormant
		// so we should set latest data as soon as possible
		state->primary_cycle = layers[6].cycle;
		state->move_weight = layers[6].weight;
		state->acceleration_weight = layers[12].weight;

		// fixes goalfeetyaw on spawn
		state->last_update_time = (record->sim_time - interfaces::global_vars->interval_per_tick);

		if (backup_record.flags & fl_onground)
		{
			state->on_ground = true;
			state->landing = false;
		}

		if (ptr->get_sequence_activity(record->sim_orig.layers[4].sequence) == act_csgo_jump)
		{
			auto land_time = ptr->simulation_time() - (record->sim_orig.layers[4].cycle / record->sim_orig.layers[4].playback_rate);

			if (land_time > last_update_time)
			{
				state->on_ground = true;
				state->landing = false;
				state->last_update_time = land_time;

				ptr->pose_parameter()[6] = 0.0f;
			}
		}
	}
	else
	{
		auto& layers = last_record->sim_orig.layers;
		state->primary_cycle = layers[6].cycle;
		state->move_weight = layers[6].weight;
		state->acceleration_weight = layers[12].weight;
	}

	if (!last_record || record->choke < 2)
	{
		ptr->abs_velocity() = ptr->velocity() = record->velocity_for_animfix;

		if (!teammate)
		{
			if (side != side_original)
				state->abs_yaw = math::normalize(ptr->eye_angles().y + (state->get_max_rotation() * side));

#if ALPHA || _DEBUG || BETA
			if (last_record)
			{
				float diff = math::normalize(last_record->eye_angles.y - record->eye_angles.y);

				record->fix_jitter_angle = std::fabsf(diff) > 10.f;
				record->last_eyeang = last_record->eye_angles.y;
				record->last_eyeang_diff = diff;

				if (record->old_diff != record->last_eyeang_diff)
				{
					record->old_diff = record->last_eyeang_diff;
					record->last_eyeang_diff_time = ptr->simulation_time();
				}
			}
#endif
		}

		this->force_update();
	}
	else
	{
		ptr->lby() = last_record->lby;
		ptr->thirdperson_recoil() = last_record->thirdperson_recoil;

		for (int i = 0; i < record->choke; ++i)
		{
			float new_simtime = record->old_sim_time + math::ticks_to_time(i + 1);
			auto sim_tick = math::time_to_ticks(new_simtime);

			ptr->abs_velocity() = ptr->velocity() = record->velocity;

			if (record->on_ground)
				ptr->flags() |= fl_onground;
			else
				ptr->flags() &= ~fl_onground;

			if (!teammate)
			{
				if (side != side_original)
					state->abs_yaw = math::normalize(ptr->eye_angles().y + (state->get_max_rotation() * side));
				else
					resolver::apply_side(ptr, record->choke);

#if ALPHA || _DEBUG || BETA
				if (last_record)
				{
					float diff = math::normalize(last_record->eye_angles.y - record->eye_angles.y);

					record->fix_jitter_angle = std::fabsf(diff) > 10.f;
					record->last_eyeang = math::normalize(record->eye_angles.y);
					record->last_eyeang_diff = diff;

					if (record->old_diff != record->last_eyeang_diff)
					{
						record->old_diff = record->last_eyeang_diff;
						record->last_eyeang_diff_time = ptr->simulation_time();
					}
				}
#endif
			}

			if (record->shooting)
			{
				if (record->last_shot_time <= new_simtime)
				{
					ptr->lby() = record->lby;
					ptr->thirdperson_recoil() = record->thirdperson_recoil;
				}
			}

			float old_simtime = ptr->simulation_time();
			ptr->simulation_time() = new_simtime;
			this->force_update();
			ptr->simulation_time() = old_simtime;
		}
	}

	if (this->last_record)
	{
		record->sim_orig.layers[12].weight = this->last_record->sim_orig.layers[12].weight;
		record->sim_orig.layers[12].cycle = this->last_record->sim_orig.layers[12].cycle;
	}
	else
	{
		record->sim_orig.layers[12].weight = 0.f;
		record->sim_orig.layers[12].cycle = 0.f;
	}

	const auto backup_origin = ptr->get_abs_origin();
	ptr->set_abs_origin(ptr->origin());
	{
		auto current_side = get_data_by_index(record, side);

		ptr->store_layer(current_side->layers);
		ptr->set_layer(record->sim_orig.layers);

		ptr->setup_uninterpolated_bones(current_side->bone);

		if (side != side_original && side != side_zero)
		{
			auto old_angles = ptr->eye_angles().z;

			ptr->eye_angles().z = 50.f * -side;
			memcpy_fast(current_side->roll_bone, current_side->bone, sizeof(current_side->bone));

			g_ctx.modify_body[ptr->index()] = true;
			func_ptrs::modify_body_yaw(ptr, current_side->bone, 0x7FF00);
			g_ctx.modify_body[ptr->index()] = false;

			ptr->eye_angles().z = old_angles;
		}
		else
		{
			if (side == side_original)
			{
				auto old_angle = ptr->eye_angles();

				adjust_roll_angle(record);

				ptr->eye_angles() = record->eye_angles;

				g_ctx.modify_body[ptr->index()] = true;
				func_ptrs::modify_body_yaw(ptr, current_side->bone, 0x7FF00);
				g_ctx.modify_body[ptr->index()] = false;

				ptr->eye_angles() = old_angle;
			}
		}
	}
	ptr->set_abs_origin(backup_origin);

	ptr->store_poses(record->poses);

	g_animation_fix->sim_step_update(ptr);

	restore_globals(cur_time);
	restore_globals(frame_time);
}

void c_animation_fix::anim_player_t::build_bones(records_t* record, simulated_data_t* sim)
{
	ptr->setup_uninterpolated_bones(sim->bone, { 0.f, 9999.f, 0.f });
}

void c_animation_fix::anim_player_t::update_animations()
{
	auto backup_lower_body_yaw_target = this->ptr->lby();
	auto backup_duck_amount = this->ptr->duck_amount();
	auto backup_flags = this->ptr->flags();
	auto backup_eflags = this->ptr->e_flags();

	auto backup_curtime = interfaces::global_vars->cur_time; //-V807
	auto backup_frametime = interfaces::global_vars->frame_time;
	auto backup_realtime = interfaces::global_vars->real_time;
	auto backup_framecount = interfaces::global_vars->frame_count;
	auto backup_tickcount = interfaces::global_vars->tick_count;
	auto backup_interpolation_amount = interfaces::global_vars->interpolation_amount;

	interfaces::global_vars->cur_time = this->ptr->simulation_time();
	interfaces::global_vars->frame_time = interfaces::global_vars->interval_per_tick;

	while (records.size() > 27)
		records.pop_back();
	old_record = nullptr;

	if (records.size() > 0)
	{
		last_record = &records.front();

		if (records.size() >= 2)
			old_record = &records.at(1);
	}

	backup_record.update_record(ptr);

	auto& record = records.emplace_front();
	record.update_record(ptr);
	record.update_dormant(dormant_ticks);
	record.update_shot(last_record);

	if (dormant_ticks < 1)
		dormant_ticks++;

	this->fix_land(&record);
	this->fix_velocity(&record);

	auto e = this;

	if (old_record)
	{
		auto ticks_chocked = 1;
		auto simulation_ticks = math::time_to_ticks(e->ptr->simulation_time() - old_record->old_sim_time);

		if (simulation_ticks > 0 && simulation_ticks < 17)
			ticks_chocked = simulation_ticks;

		if (ticks_chocked > 1)
		{
			if (this->ptr->animstate()->last_update_frame == interfaces::global_vars->frame_count)
				this->ptr->animstate()->last_update_frame -= 1;

			if (this->ptr->animstate()->last_update_time == interfaces::global_vars->cur_time)
				this->ptr->animstate()->last_update_time += math::ticks_to_time(1);


			if (this->ptr->anim_overlay()[4].cycle < 0.5f && (!(e->ptr->flags() & fl_onground) || !(old_record->ptr->flags() & fl_onground)))
			{
				land_time = e->ptr->simulation_time() - this->ptr->anim_overlay()[4].playback_rate * this->ptr->anim_overlay()[4].cycle;
				land_in_cycle = land_time >= old_record->sim_time;
			}

			auto duck_amount_per_tick = (e->ptr->duck_amount() - old_record->duck_amount) / ticks_chocked;

			for (auto i = 0; i < ticks_chocked; ++i)
			{
				auto simulated_time = old_record->sim_time + math::ticks_to_time(i);

				if (duck_amount_per_tick != 0.f) //-V550
					e->ptr->duck_amount() = old_record->duck_amount + duck_amount_per_tick * (float)i;

				on_ground = e->ptr->flags() & fl_onground;

				if (land_in_cycle && !is_landed)
				{
					if (land_time <= simulated_time)
					{
						is_landed = true;
						on_ground = true;
					}
					else
						on_ground = old_record->flags & fl_onground;
				}

				if (on_ground)
					e->ptr->flags() |= fl_onground;
				else
					e->ptr->flags() &= ~fl_onground;

				//land fix creds lw4
				auto v490 = e->ptr->get_sequence_activity(record.ptr->anim_overlay()[5].sequence);

				if (record.ptr->anim_overlay()[5].sequence == old_record->ptr->anim_overlay()[5].sequence && (old_record->ptr->anim_overlay()[5].weight != 0.0f || record.ptr->anim_overlay()[5].weight == 0.0f)
					|| !(v490 == act_csgo_land_light || v490 == act_csgo_land_heavy)) {
					if ((record.flags & 1) != 0 && (old_record->flags & fl_onground) == 0)
						e->ptr->flags() &= ~fl_onground;
				}
				else
					e->ptr->flags() |= fl_onground;

				auto simulated_ticks = math::time_to_ticks(simulated_time);

				interfaces::global_vars->real_time = simulated_time;
				interfaces::global_vars->cur_time = simulated_time;
				interfaces::global_vars->frame_count = simulated_ticks;
				interfaces::global_vars->tick_count = simulated_ticks;
				interfaces::global_vars->interpolation_amount = 0.0f;
				interfaces::global_vars->real_time = backup_realtime;
				interfaces::global_vars->cur_time = backup_curtime;
				interfaces::global_vars->frame_count = backup_framecount;
				interfaces::global_vars->tick_count = backup_tickcount;
				interfaces::global_vars->interpolation_amount = backup_interpolation_amount;
			}
		}
	}

	if (!teammate)
	{
		resolver::prepare_side(ptr, &record);
		//resolver::prepare_side(ptr, &record, old_record);

		c_animstate old_state{};
		old_state = *ptr->animstate();

		this->simulate_animation_side(&record, side_right);
		*ptr->animstate() = old_state;

		this->simulate_animation_side(&record, side_left);
		*ptr->animstate() = old_state;

		this->simulate_animation_side(&record, side_zero);
		*ptr->animstate() = old_state;
	}

	this->simulate_animation_side(&record);

	if (g_ctx.lagcomp)
	{
		if (old_simulation_time > record.sim_time)
		{
			record.shooting = false;
			record.last_shot_time = 0.f;
			record.shifting = true;
		}
		else
			old_simulation_time = record.sim_time;

		if (!record.shifting && last_record)
			record.breaking_lc = record.choke > 3 && (record.origin - last_record->origin).length_sqr() > 4096.f;
	}
	else
		record.shifting = record.breaking_lc = false;

	last_record = nullptr;
	old_record = nullptr;
	backup_record.restore(ptr);
}

void thread_anim_update(c_animation_fix::anim_player_t* player)
{
	player->update_animations();
}

void c_animation_fix::force_data_for_render()
{
	if (!g_ctx.in_game || !g_ctx.local || g_ctx.uninject)
		return;

	auto& players = g_listener_entity->get_entity(ent_player);
	if (players.empty())
		return;

	for (auto& player : players)
	{
		auto entity = (c_csplayer*)player.entity;
		if (!entity || entity == g_ctx.local)
			continue;

		if (!entity->is_alive())
		{
			g_ctx.setup_bones[entity->index()] = g_ctx.modify_body[entity->index()] = true;
			continue;
		}

		auto animation_player = this->get_animation_player(entity->index());
		if (!animation_player || animation_player->records.empty() || animation_player->dormant_ticks < 1)
		{
			g_ctx.setup_bones[entity->index()] = g_ctx.modify_body[entity->index()] = true;
			continue;
		}

		auto first_record = &animation_player->records.front();
		if (!first_record || !first_record->sim_orig.bone)
		{
			g_ctx.setup_bones[entity->index()] = g_ctx.modify_body[entity->index()] = true;
			continue;
		}

		/*	main_utils::draw_hitbox(entity, first_record->sim_left.bone, 0, 0, true);
			main_utils::draw_hitbox(entity, first_record->sim_right.bone, 1, 0, true);
			main_utils::draw_hitbox(entity, first_record->sim_zero.bone, 1, 1, true);
			main_utils::draw_hitbox(entity, first_record->sim_orig.bone, 0, 1, true);*/

		memcpy_fast(first_record->render_bones, first_record->sim_orig.bone, sizeof(first_record->render_bones));

		math::change_matrix_position(first_record->render_bones, 128, first_record->origin, entity->get_render_origin());

		entity->interpolate_moveparent_pos();
		entity->set_bone_cache(first_record->render_bones);
		entity->attachments_helper();
	}
}

void c_animation_fix::fix_pvs()
{
	for (int i = 1; i <= interfaces::global_vars->max_clients; i++) {
		auto pCurEntity = static_cast<c_csplayer*>(interfaces::entity_list->get_entity(i));

		if (pCurEntity == g_ctx.local)
			continue;

		if (!pCurEntity
			|| !pCurEntity->is_player()
			|| pCurEntity->index() == interfaces::engine->get_local_player())
			continue;

		*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pCurEntity) + 0xA30) = interfaces::global_vars->frame_count;
		*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pCurEntity) + 0xA28) = 0;
	}
}

void c_animation_fix::apply_interpolation_flags(c_csplayer* e)
{
	auto map = e->var_mapping();
	if (map == nullptr)
		return;
	for (auto j = 0; j < map->m_nInterpolatedEntries; j++)
		map->m_Entries[j].m_bNeedsToInterpolate = false;
}

void c_animation_fix::on_net_update_and_render_after(int stage)
{
	if (!g_ctx.in_game || !g_ctx.local || g_ctx.uninject)
		return;

	auto& players = g_listener_entity->get_entity(ent_player);
	if (players.empty())
		return;

	switch (stage)
	{
	case frame_net_update_postdataupdate_end:
		for (auto& player : players)
		{
			auto ptr = (c_csplayer*)player.entity;
			if (!ptr)
				continue;
			apply_interpolation_flags(ptr);
		}

		break;

	case frame_net_update_end:
	{
		patterns::cl_fireevents.as<void(*)()>()();

		//	g_ctx.local->personal_data_public_level() = 25120;

		g_rage_bot->on_pre_predict();

		std::vector<std::uint64_t> tasks{};

		for (auto& player : players)
		{
			auto ptr = (c_csplayer*)player.entity;
			if (!ptr)
				continue;

			if (ptr == g_ctx.local)
			{
				if (ptr->is_alive())
					esp_share::share_player_position(ptr);

				continue;
			}

			auto anim_player = this->get_animation_player(ptr->index());
			if (anim_player->ptr != ptr)
			{
				anim_player->reset_data();
				anim_player->ptr = ptr;
				continue;
			}

			if (!ptr->is_alive())
			{
				if (!anim_player->teammate)
				{
					//	resolver::reset_info(ptr);
					g_rage_bot->missed_shots[ptr->index()] = 0;
				}

				anim_player->ptr = nullptr;
				continue;
			}

			if (g_cfg.misc.force_radar && ptr->team() != g_ctx.local->team())
				ptr->target_spotted() = true;

			if (ptr->dormant())
			{
				anim_player->dormant_ticks = 0;

				//	if (!anim_player->teammate)
				//		resolver::reset_info(ptr);

				anim_player->records.clear();
				anim_player->last_record = nullptr;
				anim_player->old_record = nullptr;

				continue;
			}

			auto& layer = ptr->anim_overlay()[11];
			if (layer.cycle == anim_player->old_aliveloop_cycle)
			{
				ptr->simulation_time() = ptr->old_simtime();
				continue;
			}

			anim_player->old_aliveloop_cycle = layer.cycle;

			if (ptr->simulation_time() <= ptr->old_simtime())
				continue;

			esp_share::share_player_position(ptr);

			auto state = ptr->animstate();
			if (!state)
				continue;

			if (anim_player->old_spawn_time != ptr->spawn_time())
			{
				state->player = ptr;
				state->reset();

				anim_player->old_spawn_time = ptr->spawn_time();
			}

			anim_player->teammate = g_ctx.local && g_ctx.local->team() == ptr->team();

			tasks.emplace_back(g_thread_pool->add_task(thread_anim_update, anim_player));
		}

		for (auto& task : tasks)
			g_thread_pool->wait(task);

		for (auto& player : players)
		{
			auto ptr = (c_csplayer*)player.entity;
			if (!ptr)
				continue;

			if (ptr == g_ctx.local)
				continue;

			auto anim_player = this->get_animation_player(ptr->index());
			if (anim_player->ptr != ptr)
				continue;

			if (!ptr->is_alive())
				continue;

			if (ptr->dormant())
				continue;

			if (ptr->simulation_time() <= ptr->old_simtime())
				continue;

			if (*(int*)((std::uintptr_t)ptr + 0x28C0) != -1)
			{
				using fn = void(__thiscall*)(void*, int);
				g_memory->getvfunc<fn>(ptr, 108)(ptr, 1);
			}
		}
	}
	break;

	case frame_render_start:
		for (auto& player : players)
		{
			auto ptr = (c_csplayer*)player.entity;
			if (!ptr)
				continue;
			ptr->set_abs_origin(ptr->origin());
			fix_pvs();
		}
		break;
	}
}

void c_animation_fix::update_valid_ticks()
{
	auto& players = g_listener_entity->get_entity(ent_player);
	if (players.empty())
		return;

	for (auto& player : players)
	{
		auto entity = (c_csplayer*)player.entity;
		if (!entity)
			continue;

		if (entity == g_ctx.local || entity->team() == g_ctx.local->team())
			continue;

		if (!entity->is_alive() || entity->dormant())
			continue;

		auto anim_player = this->get_animation_player(entity->index());
		if (anim_player->records.empty())
			continue;

		for (auto& i : anim_player->records)
			i.valid_tick = g_ctx.is_alive ? i.is_valid() : true;
	}
}

bool records_t::is_valid(bool deadtime)
{
	auto netchan = interfaces::engine->get_net_channel_info();
	if (!netchan)
		return false;

	if (!g_ctx.lagcomp)
		return true;

	if (this->shifting)
		return false;

	float time = g_ctx.is_alive ? g_ctx.predicted_curtime : interfaces::global_vars->cur_time;
	float outgoing = g_ctx.real_ping;

	float correct = 0.f;

	correct += g_ctx.ping;
	correct += g_ctx.lerp_time;
	correct = std::clamp<float>(correct, 0.0f, cvars::sv_maxunlag->get_float());

	float delta_time = correct - (time - this->sim_time);

	if (std::fabsf(delta_time) > 0.2f)
		return false;

	auto extra_choke = 0;

	if (g_anti_aim->is_fake_ducking())
		extra_choke = 14 - interfaces::client_state->choked_commands;

	auto server_tickcount = interfaces::global_vars->tick_count + g_ctx.ping + extra_choke;
	auto dead_time = (int)(float)((float)((int)((float)((float)server_tickcount
		* interfaces::global_vars->interval_per_tick) - 0.2f) / interfaces::global_vars->interval_per_tick) + 0.5f);

	if (math::time_to_ticks(this->sim_time + g_ctx.lerp_time) < dead_time)
		return false;

	return true;
}