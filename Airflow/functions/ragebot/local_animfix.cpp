#include "local_animfix.h"
#include "../features.h"
#include "../config_vars.h"


void c_local_animation_fix::update_fake()
{
	auto fake = &fake_animstate;
	auto vars = &updated_vars;

	if (!g_cfg.visuals.chams[c_fake].enable)
	{
		if (fake->state)
		{
			interfaces::memory->free(fake->state);
			fake->state = nullptr;
		}

		fake->reset_state = false;
		return;
	}

	if (fake->old_spawn_time != g_ctx.local->spawn_time())
	{
		interfaces::memory->free(fake->state);
		fake->state = nullptr;

		fake->old_spawn_time = g_ctx.local->spawn_time();
		fake->reset_state = false;
		return;
	}

	if (fake->old_ref_handle != g_ctx.local->get_ref_handle())
	{
		interfaces::memory->free(fake->state);
		fake->state = nullptr;

		fake->old_ref_handle = g_ctx.local->get_ref_handle();
		fake->reset_state = false;
		return;
	}

	if (!fake->reset_state)
	{
		if (!fake->state)
			fake->state = (c_animstate*)interfaces::memory->alloc(sizeof(c_animstate));

		fake->state->create(g_ctx.local);
		fake->reset_state = true;
	}

	if (!fake->reset_state || !fake->state)
		return;

	if (fake->state->last_update_time == interfaces::global_vars->cur_time)
		fake->state->last_update_time = interfaces::global_vars->cur_time - 1.f;

	if (fake->state->last_update_frame == interfaces::global_vars->frame_count)
		fake->state->last_update_frame = interfaces::global_vars->frame_count - 1;

	if (!(g_cfg.misc.animation_changes & 8))
		g_ctx.local->anim_overlay()[12].weight = 0.f;

	vector3d old_render_angles = g_ctx.local->render_angles();
	vector3d old_eye_angles = g_ctx.local->eye_angles();

	fake->state->update(g_ctx.fake_angle);

	g_ctx.local->render_angles() = g_ctx.fake_angle;
	g_ctx.local->eye_angles() = g_ctx.fake_angle;

	g_ctx.local->setup_uninterpolated_bones(vars->fake_bones);

	const auto& render_origin = g_ctx.local->get_render_origin();
	math::change_matrix_position(vars->fake_bones, 256, render_origin, {});

	g_ctx.local->render_angles() = old_render_angles;
	g_ctx.local->eye_angles() = old_eye_angles;
}


void c_local_animation_fix::update_viewmodel()
{
	auto viewmodel = g_ctx.local->get_view_model();
	if (viewmodel)
		func_ptrs::update_all_viewmodel_addons(viewmodel);
}

void c_local_animation_fix::update()
{
	if (!g_ctx.cmd || !g_ctx.local || !g_ctx.local->is_alive())
		return;

	auto state = g_ctx.local->animstate();
	if (!state)
		return;

	static float last_land_time = 0.f;
	auto vars = &updated_vars;

	static float old_spawn_time = 0.f;

	if (old_spawn_time != g_ctx.local->spawn_time())
	{
		vars->old_movetype = 0;
		vars->old_flags = 0; g_ctx.local->store_layer(backup_vars.sent_layers);

		last_land_time = 0.f;

		std::memset(vars->sent_layers, 0, sizeof(vars->sent_layers));
		std::memset(vars->updated_layers, 0, sizeof(vars->updated_layers));

		bool old_update = g_ctx.local->client_side_animation();

		g_ctx.update[g_ctx.local->index()] = true;
		g_ctx.local->client_side_animation() = true;
		g_ctx.local->update_clientside_animation();
		g_ctx.update[g_ctx.local->index()] = false;
		g_ctx.local->client_side_animation() = old_update;

		old_spawn_time = g_ctx.local->spawn_time();
		return;
	}

	//g_ctx.local->draw_server_hitbox();

	update_viewmodel();

	if (!*g_ctx.send_packet)
		return;

	const auto old_render_angles = g_ctx.local->render_angles();
	const auto old_eye_angles = g_ctx.local->eye_angles();

	g_ctx.local->store_layer(backup_vars.sent_layers);

	state->primary_cycle = backup_vars.rebuilt_state.primary_cycle;
	state->move_weight = backup_vars.rebuilt_state.move_weight;

	state->strafe_sequence = backup_vars.rebuilt_state.strafe_sequence;
	state->strafe_change_weight = backup_vars.rebuilt_state.strafe_change_weight;
	state->strafe_change_cycle = backup_vars.rebuilt_state.strafe_change_cycle;
	state->acceleration_weight = backup_vars.rebuilt_state.acceleration_weight;

	backup_globals(cur_time);
	backup_globals(frame_count);
	backup_globals(frame_time);

	{
		interfaces::global_vars->cur_time = math::ticks_to_time(g_ctx.tick_base);
		interfaces::global_vars->frame_time = interfaces::global_vars->interval_per_tick;

		update_fake();
	}

	c_engine_prediction::netvars_t vars_to_restore{};
	for (int i = 0; i <= interfaces::client_state->choked_commands + 1; ++i)
	{
		vars_to_restore.fill();

		int command_number = interfaces::client_state->last_outgoing_command + i;
		c_usercmd* command{ nullptr };

		if (command_number >= g_ctx.shot_cmd && g_ctx.shot_cmd >= command_number - interfaces::client_state->choked_commands)
			command = interfaces::input->get_user_cmd(g_ctx.shot_cmd % 150);
		else
			command = interfaces::input->get_user_cmd(command_number % 150);

		auto predicted_vars = g_engine_prediction->get_predicted_vars(command_number);
		predicted_vars->set_for_animations();

		g_ctx.local->render_angles() = g_ctx.local->eye_angles() = command->viewangles;

		{
			int previous_jump_buttons = g_ctx.jump_buttons;
			auto previous_cmd = g_ctx.cmd;
			g_ctx.jump_buttons = command->buttons;
			g_ctx.cmd = command;

			int previous_command_number = g_ctx.local->cmd_context().cmd_number;
			g_ctx.local->cmd_context().cmd_number = command_number;
			g_ctx.local->cmd_context().user_cmd = *g_ctx.cmd;

			interfaces::global_vars->cur_time = math::ticks_to_time(predicted_vars->tickbase);
			interfaces::global_vars->frame_count = predicted_vars->tickbase;
			interfaces::global_vars->frame_time = interfaces::global_vars->interval_per_tick;

			auto anim_overlay = g_ctx.local->anim_overlay();
			anim_overlay[animation_layer_movement_jump_or_fall] = vars->updated_layers[animation_layer_movement_jump_or_fall];
			anim_overlay[animation_layer_movement_land_or_climb] = vars->updated_layers[animation_layer_movement_land_or_climb];

			server_animations::run(state, vars, anim_overlay);

			{
				bool old_update = g_ctx.local->client_side_animation();
				g_ctx.update[g_ctx.local->index()] = true;

				g_ctx.local->client_side_animation() = true;
				g_ctx.local->update_clientside_animation();
				g_ctx.update[g_ctx.local->index()] = false;

				g_ctx.local->client_side_animation() = old_update;
			}

			g_ctx.local->store_layer(vars->updated_layers);

			g_ctx.local->cmd_context().cmd_number = previous_command_number;
			g_ctx.jump_buttons = previous_jump_buttons;
			g_ctx.cmd = previous_cmd;
		}

		vars_to_restore.set_for_animations();
	}

	vars->foot_yaw = state->abs_yaw;
	vars->v_angle = g_ctx.local->render_angles();

	auto& rebuilt_state = vars->rebuilt_state;
	auto speed_portion_walk = rebuilt_state.speed_as_portion_of_walk_top_speed;
	auto speed_portion_duck = rebuilt_state.speed_as_portion_of_crouch_top_speed;
	auto transition = rebuilt_state.walk_run_transition;
	auto duck_amount = rebuilt_state.anim_duck_amount;

	vars->aim_matrix_width_range = math::interpolate_inversed(std::clamp(speed_portion_walk, 0.f, 1.f), 1.f,
		math::interpolate_inversed(transition, 0.8f, 0.5f));

	if (duck_amount > 0)
		vars->aim_matrix_width_range = math::interpolate_inversed(duck_amount * std::clamp(speed_portion_duck, 0.f, 1.f),
			vars->aim_matrix_width_range, 0.5f);

	vars->max_desync_range = state->aim_yaw_max * vars->aim_matrix_width_range;

	g_ctx.local->store_layer(vars->sent_layers);
	g_ctx.local->store_poses(vars->sent_pose_params);

	std::memcpy(&backup_vars.rebuilt_state, state, sizeof(backup_vars.rebuilt_state));

	g_ctx.local->set_layer(backup_vars.sent_layers);

	restore_globals(cur_time);
	restore_globals(frame_count);
	restore_globals(frame_time);

	g_ctx.local->render_angles() = old_render_angles;
	g_ctx.local->render_angles() = old_eye_angles;
}

void c_local_animation_fix::force_data_for_render()
{
	if (!g_ctx.in_game || !g_ctx.cmd || !g_ctx.local || !g_ctx.local->is_alive() || g_ctx.uninject)
		return;

	auto vars = &this->updated_vars;

	const auto old_angles = g_ctx.local->render_angles();
	const auto old_eye_angles = g_ctx.local->eye_angles();

	static float last_land_time = 0.f;
	static local_anims_t backup_vars{};

	g_ctx.local->store_layer(backup_vars.sent_layers);
	g_ctx.local->store_poses(backup_vars.sent_pose_params);

	{
		auto old_eyeangles = g_ctx.local->eye_angles();
		auto old_vangle = g_ctx.local->render_angles();

		g_ctx.local->render_angles() = g_ctx.local->eye_angles() = vars->v_angle;

		g_ctx.local->set_layer(vars->sent_layers);
		g_ctx.local->set_poses(vars->sent_pose_params);

		auto layers = g_ctx.local->anim_overlay();
		if (!(g_cfg.misc.animation_changes & 8))
			layers[animation_layer_lean].weight = 0.f;

		auto& poses = g_ctx.local->pose_parameter();
		if (!g_utils->on_ground())
		{
			if (g_cfg.misc.animation_changes & 2)
				poses[6] = 1.f;

			last_land_time = interfaces::global_vars->cur_time;
		}
		else if (g_cfg.misc.animation_changes & 1 && (std::fabsf(interfaces::global_vars->cur_time - last_land_time) < 1.f))
			poses[12] = 0.5f;

		if (g_cfg.misc.animation_changes & 4)
			poses[0] = 1.f;

		backup_globals(cur_time);
		backup_globals(frame_time);

		interfaces::global_vars->cur_time = g_ctx.tick_base;
		interfaces::global_vars->frame_time = interfaces::global_vars->interval_per_tick;

		g_ctx.local->set_abs_angles({ 0.f, vars->foot_yaw, 0.f });
		g_ctx.local->setup_uninterpolated_bones(nullptr);

		g_ctx.local->eye_angles() = old_eyeangles;
		g_ctx.local->render_angles() = old_vangle;

		restore_globals(cur_time);
		restore_globals(frame_time);
	}

	g_ctx.local->set_layer(backup_vars.sent_layers);
	g_ctx.local->set_poses(backup_vars.sent_pose_params);
}