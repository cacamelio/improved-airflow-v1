#include "../features.h"
#include "../../includes.h"
#include "server_animations.h"

#define CSGO_ANIM_LOWER_CATCHUP_IDLE	100.0f
#define CSGO_ANIM_AIM_NARROW_WALK	0.8f
#define CSGO_ANIM_AIM_NARROW_RUN	0.5f
#define CSGO_ANIM_AIM_NARROW_CROUCHMOVING	0.5f
#define CSGO_ANIM_LOWER_CATCHUP_WITHIN	3.0f
#define CSGO_ANIM_LOWER_REALIGN_DELAY	1.1f
#define CSGO_ANIM_READJUST_THRESHOLD	120.0f
#define EIGHT_WAY_WIDTH 22.5f
#define CSGO_ANIM_WALK_TO_RUN_TRANSITION_SPEED 2.0f
#define CSGO_ANIM_ONGROUND_FUZZY_APPROACH 8.0f
#define CSGO_ANIM_ONGROUND_FUZZY_APPROACH_CROUCH 16.0f
#define CSGO_ANIM_LADDER_CLIMB_COVERAGE 100.0f
#define CSGO_ANIM_RUN_ANIM_PLAYBACK_MULTIPLIER 0.85f
#define CS_PLAYER_SPEED_RUN 260.0f
#define CS_PLAYER_SPEED_VIP 227.0f
#define CS_PLAYER_SPEED_SHIELD 160.0f
#define CS_PLAYER_SPEED_HAS_HOSTAGE 200.0f
#define CS_PLAYER_SPEED_STOPPED 1.0f
#define CS_PLAYER_SPEED_OBSERVER 900.0f
#define CS_PLAYER_SPEED_DUCK_MODIFIER 0.34f
#define CS_PLAYER_SPEED_WALK_MODIFIER 0.52f
#define CS_PLAYER_SPEED_CLIMB_MODIFIER 0.34f
#define CS_PLAYER_HEAVYARMOR_FLINCH_MODIFIER 0.5f
#define CS_PLAYER_DUCK_SPEED_IDEAL 8.0f

namespace server_animations
{
    __forceinline void set_sequence(c_animstate* state, c_animation_layers* layer, const int& activity, int idx)
    {
        state->set_layer_sequence(layer, state->select_sequence_from_activity_modifier(activity), idx);
    }
    void setup_movement(c_animstate* state, local_anims_t* local_anim, c_animation_layers* layers)
    {
        auto rebuilt_state = &local_anim->rebuilt_state;

        auto land_or_climb = &layers[animation_layer_movement_land_or_climb];
        auto jump_or_fall = &layers[animation_layer_movement_jump_or_fall];

        auto ground_entity = (c_baseentity*)interfaces::entity_list->get_entity_handle(g_ctx.local->ground_entity());

        bool old_on_ladder = local_anim->old_movetype == movetype_ladder;
        bool on_ladder = g_ctx.local->move_type() == movetype_ladder;

        auto& flags = g_ctx.local->flags();

        bool landed = (flags & fl_onground) && !(local_anim->old_flags & fl_onground);
        bool jumped = !(flags & fl_onground) && (local_anim->old_flags & fl_onground);

        local_anim->old_flags = flags;
        local_anim->old_movetype = g_ctx.local->move_type();

        auto entered_ladder = on_ladder && !old_on_ladder;
        auto left_ladder = !on_ladder && old_on_ladder;

        if (entered_ladder)
            state->set_layer_sequence(land_or_climb, state->select_sequence_from_activity_modifier(act_csgo_climb_ladder), animation_layer_movement_jump_or_fall);

        if ((g_ctx.cmd->buttons & in_jump) && ground_entity)
            state->set_layer_sequence(jump_or_fall, state->select_sequence_from_activity_modifier(act_csgo_jump), animation_layer_movement_jump_or_fall);

        if (flags & fl_onground)
        {
            if (!local_anim->landing && landed)
            {
                int activity = state->duration_in_air > 1.f ? act_csgo_land_heavy : act_csgo_land_light;
                state->set_layer_sequence(land_or_climb, state->select_sequence_from_activity_modifier(activity), animation_layer_movement_land_or_climb);

                local_anim->landing = true;
            }
        }
        else
        {
            if (!(g_ctx.cmd->buttons & in_jump) && (jumped || left_ladder))
                state->set_layer_sequence(jump_or_fall, state->select_sequence_from_activity_modifier(act_csgo_fall), animation_layer_movement_jump_or_fall);

            local_anim->landing = false;
        }

    }

    void setup_velocity(c_animstate* state, float curtime) {
        auto player = reinterpret_cast<c_csplayer*>(state->player);
        auto hdr = player->get_studio_hdr();
        if (!hdr)
            return;

        auto weapon = (c_basecombatweapon*)(interfaces::entity_list->get_entity_handle(player->active_weapon()));
        if (!weapon)
            return;

        auto weapon_info = interfaces::weapon_system->get_weapon_data(weapon->item_definition_index());
        auto max_speed = weapon && weapon_info
            ? std::max<float>((player->is_scoped() ? weapon_info->max_speed_alt : weapon_info->max_speed), 0.001f)
            : CS_PLAYER_SPEED_RUN;

        auto abs_velocity = player->velocity();

        // Save vertical velocity and discard z-component
        state->velocity_length_z = abs_velocity.z;
        abs_velocity.z = 0.f;

        // Detect acceleration
        state->player_is_accelerating = (state->velocity_last.length_sqr() < abs_velocity.length_sqr());

        // Smooth velocity transition
        state->velocity = math::approach(abs_velocity, state->velocity, state->last_update_increment * 2000);
        state->velocity_normalized = state->velocity.normalized();

        // Compute horizontal velocity length
        state->velocity_length_xy = std::min<float>(state->velocity.length(), CS_PLAYER_SPEED_RUN);

        if (state->velocity_length_xy > 0) {
            state->velocity_normalized_non_zero = state->velocity_normalized;
        }

        // Compute normalized speed portions
        float flMaxSpeedRun = max_speed > 0 ? max_speed : CS_PLAYER_SPEED_RUN;
        state->speed_as_portion_of_run_top_speed = std::clamp<float>(state->velocity_length_xy / flMaxSpeedRun, 0, 1);
        state->speed_as_portion_of_walk_top_speed = state->velocity_length_xy / (flMaxSpeedRun * CS_PLAYER_SPEED_WALK_MODIFIER);
        state->speed_as_portion_of_crouch_top_speed = state->velocity_length_xy / (flMaxSpeedRun * CS_PLAYER_SPEED_DUCK_MODIFIER);

        if (state->speed_as_portion_of_walk_top_speed >= 1) {
            state->static_approach_speed = state->velocity_length_xy;
        }
        else if (state->speed_as_portion_of_walk_top_speed < 0.5f) {
            state->static_approach_speed = math::approach(80, state->static_approach_speed, state->last_update_increment * 60);
        }

        // Movement frame detection
        bool started_moving_this_frame = false;
        bool stopped_moving_this_frame = false;

        if (state->velocity_length_xy > 0) {
            started_moving_this_frame = (state->duration_moving <= 0);
            state->duration_still = 0;
            state->duration_moving += state->last_update_increment;
        }
        else {
            stopped_moving_this_frame = (state->duration_still <= 0);
            state->duration_moving = 0;
            state->duration_still += state->last_update_increment;
        }

        auto adjust = &player->anim_overlay()[animation_layer_adjust];

        if (!state->adjust_started && stopped_moving_this_frame && state->on_ground && !state->on_ladder && !state->landing && state->stutter_step < 50.f) {
            state->set_layer_sequence(adjust, state->select_sequence_from_activity_modifier(act_csgo_idle_adjust_stoppedmoving), animation_layer_adjust);
            state->adjust_started = true;
        }

        int layer_activity = player->get_sequence_activity(adjust->sequence);
        if (layer_activity == act_csgo_idle_adjust_stoppedmoving || layer_activity == act_csgo_idle_turn_balanceadjust) {
            if (state->adjust_started && state->speed_as_portion_of_crouch_top_speed <= 0.25f) {
                state->increment_layer_cycle(adjust, false);
                state->set_layer_weight(adjust, state->get_layer_ideal_weight_from_seq_cycle(adjust));
                state->adjust_started = !(state->is_layer_sequence_finished(adjust, state->last_update_increment));
            }
            else {
                state->adjust_started = false;
                state->set_layer_weight(adjust, math::approach(0, adjust->weight, state->last_update_increment * 5.f));
            }
        }

        // Yaw and aim matrix adjustments
        state->abs_yaw_last = state->abs_yaw;
        state->abs_yaw = std::clamp<float>(state->abs_yaw, -360, 360);
        auto eye_delta = math::angle_diff(state->eye_yaw, state->abs_yaw);

        float aim_matrix_width_range = std::lerp(std::clamp(state->speed_as_portion_of_walk_top_speed, 0.f, 1.f), 1.f,
            std::lerp(state->walk_run_transition, 0.8f, 0.5f));

        if (state->anim_duck_amount > 0) {
            aim_matrix_width_range = std::lerp(state->anim_duck_amount * std::clamp(state->speed_as_portion_of_crouch_top_speed, 0.f, 1.f),
                aim_matrix_width_range, 0.5f);
        }

        auto yaw_max = state->aim_yaw_max * aim_matrix_width_range;
        auto yaw_min = state->aim_yaw_min * aim_matrix_width_range;

        if (eye_delta > yaw_max) {
            state->abs_yaw = state->eye_yaw - std::abs(yaw_max);
        }
        else if (eye_delta < yaw_min) {
            state->abs_yaw = state->eye_yaw + std::abs(yaw_min);
        }
        state->abs_yaw = math::normalize(state->abs_yaw);
    }

    void setup_lean(c_animstate* state, float curtime) {
        auto player = reinterpret_cast<c_csplayer*>(state->player);
        auto lean = &player->anim_overlay()[animation_layer_lean];

        // Calculate time interval since last velocity check
        float interval = curtime - state->last_velocity_test_time;
        if (interval > 0.025f) {
            interval = min(interval, 0.1f);
            state->last_velocity_test_time = curtime;

            // Compute target acceleration
            state->target_acceleration = (player->velocity() - state->velocity_last) / interval;
            state->target_acceleration.z = 0; // Ignore vertical acceleration

            state->velocity_last = player->velocity();
        }

        // Smooth acceleration transition
        state->acceleration = math::approach(state->target_acceleration, state->acceleration, state->last_update_increment * 800.0f);

        // Convert acceleration to lean angle
       // vector3d temp;
       // math::vector_to_angles(state->acceleration, vector3d(0, 0, 1), temp);

        // Calculate acceleration weight
        float acceleration_weight = std::clamp((state->acceleration.length() / CS_PLAYER_SPEED_RUN) * state->speed_as_portion_of_run_top_speed, 0.0f, 1.0f);
        acceleration_weight *= (1.0f - state->ladder_weight);

        // Set lean pose parameter
       // state->pose_param_mappings[player_pose_param_lean_yaw].set(player, math::normalize_yaw(state->abs_yaw - temp.y));

        if (lean->sequence <= 0) {
            auto sequence = player->get_sequence_activity(lean->sequence);
            if (sequence > 0) {
                lean->sequence = sequence;
            }
        }

        // Apply acceleration weight to the lean layer
        lean->weight = acceleration_weight;
        lean->cycle = 0.0f;
    }

    void run(c_animstate* state, local_anims_t* local_anim, c_animation_layers* layers)
    {
        setup_movement(state, local_anim, layers);
        //    setup_velocity(state, interfaces::global_vars->cur_time);
        //    setup_lean(state, interfaces::global_vars->cur_time);
    }
}