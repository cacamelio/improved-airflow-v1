#include "autowall.h"

#include "../../base/sdk.h"
#include "../../base/global_context.h"

#include "../../base/sdk/entity.h"

#include "../../functions/features.h"

constexpr int shot_hull = 0x600400bu;
constexpr int shot_player = 0x4600400bu;

bool c_auto_wall::is_breakable_entity(c_baseentity* entity)
{
	if (!entity || !entity->index() || !entity->get_client_class())
		return false;

	auto client_class = entity->get_client_class();

	// on v4c map cheat shoots through this
	// ignore these fucks
	if (client_class->class_id == CBaseButton || client_class->class_id == CPhysicsProp)
		return false;

	// check if it's window by name (pasted from onetap)
	auto v3 = (int)client_class->network_name;
	if (*(DWORD*)v3 == 0x65724243)
	{
		if (*(DWORD*)(v3 + 7) == 0x53656C62)
			return true;
	}

	if (*(DWORD*)v3 == 0x73614243)
	{
		if (*(DWORD*)(v3 + 7) == 0x79746974)
			return true;
	}

	return func_ptrs::is_breakable_entity(entity);
}

bool c_auto_wall::can_hit_point(c_csplayer* entity, c_csplayer* starter, const vector3d& point, const vector3d& source, int min_damage)
{
	const auto origin_backup = starter->get_abs_origin();

	starter->set_abs_origin(vector3d(source.x, source.y, origin_backup.z));
	const auto& data = fire_bullet(starter, entity, g_ctx.weapon_info, g_ctx.weapon->is_taser(), source, point);
	starter->set_abs_origin(origin_backup);

	return data.dmg >= min_damage + 1;
}

bool c_auto_wall::trace_to_exit(const vector3d& src, const vector3d& dir, const c_game_trace& enter_trace, c_game_trace& exit_trace, c_csplayer* shooter)
{
	constexpr float MAX_DISTANCE = 90.f, STEP_SIZE = 4.f;
	float current_distance = 0.f;

	int first_contents = 0;

	do
	{
		current_distance += STEP_SIZE;
		auto new_end = src + (dir * current_distance);

		int point_contents = interfaces::engine_trace->get_point_contents(new_end, shot_player);

		if (!first_contents)
			first_contents = point_contents;

		if (!(point_contents & mask_shot_hull) || ((point_contents & contents_hitbox) && point_contents != first_contents))
		{
			auto new_start = new_end - (dir * STEP_SIZE);

			interfaces::engine_trace->trace_ray(ray_t(new_end, new_start), shot_player, nullptr, &exit_trace);
			if (exit_trace.start_solid && (exit_trace.surface.flags & surf_hitbox))
			{
				c_trace_filter filter{};
				filter.skip = shooter;

				interfaces::engine_trace->trace_ray(ray_t(src, new_start), shot_player, &filter, &exit_trace);

				if (exit_trace.did_hit() && !exit_trace.start_solid)
				{
					new_end = exit_trace.end;
					return true;
				}

				continue;
			}
			else {

				if (!exit_trace.did_hit() || exit_trace.start_solid)
				{
					if (exit_trace.entity)
					{
						if (enter_trace.did_hit_non_world_entity())
						{
							if (is_breakable_entity(enter_trace.entity))
							{
								exit_trace = enter_trace;
								exit_trace.end = src + dir;
								return true;
							}
						}
					}
				}
				else
				{
					if (is_breakable_entity(enter_trace.entity) && is_breakable_entity(exit_trace.entity))
						return true;

					if (enter_trace.surface.flags & surf_nodraw || (!(exit_trace.surface.flags & surf_nodraw) && exit_trace.plane.normal.dot(dir) <= 1.f))
					{
						const float mult_amount = exit_trace.fraction * 4.f;
						new_start -= dir * mult_amount;
						return true;
					}

					continue;
				}
			}
		}
	} while (current_distance <= MAX_DISTANCE);

	return false;
}

void c_auto_wall::clip_trace_to_players(const vector3d& start, const vector3d& end, unsigned int mask, i_trace_filter* filter, c_game_trace* tr, should_hit_fn_t should_hit)
{
	float smallest_fraction = tr->fraction;

	const auto& players = g_listener_entity->get_entity(ent_player);
	for (auto& i : players)
	{
		auto player = reinterpret_cast<c_csplayer*>(i.entity);

		if (!player || player == g_ctx.local)
			continue;

		if (player->dormant() || !player->is_alive())
			continue;

		auto collideable = player->get_collideable();
		if (!collideable)
			continue;

		auto obb_center = (collideable->get_mins() + collideable->get_maxs()) / 2.f;
		auto position = obb_center + player->origin();

		vector3d to = position - start;
		vector3d direction = end - start;
		float length = direction.normalized_float();

		float range_along = direction.dot(to);
		float range{ 0.f };
		if (range_along < 0.0f)
			range = -to.length(false);
		else if (range_along > length)
			range = -(position - end).length(false);

		else
		{
			auto on_ray = start + direction * range_along;
			range = (position - on_ray).length(false);
		}

		if (range < 0.f || range > 60.f)
			return;

		c_game_trace trace;
		interfaces::engine_trace->clip_ray_to_entity(ray_t(start, end), mask, player, &trace);

		if (trace.fraction < smallest_fraction)
		{
			*tr = trace;
			smallest_fraction = trace.fraction;
		}
	}
}

void c_auto_wall::clip_trace_to_player(const vector3d& src, const vector3d& dst, c_game_trace& trace, c_csplayer* const player, const should_hit_fn_t& should_hit_fn)
{
	const auto pos = player->origin() + (player->bb_mins() + player->bb_maxs()) * 0.5f;
	vector3d to = pos - src;
	vector3d direction = dst - src;
	float length = direction.normalized_float();

	float range_along = direction.dot(to);
	float range{ 0.f };
	if (range_along < 0.0f)
		range = -to.length(false);
	else if (range_along > length)
		range = -(pos - dst).length(false);

	else
	{
		auto on_ray = src + direction * range_along;
		range = (pos - on_ray).length(false);
	}

	if (range < 0.f || range > 60.f)
		return;

	c_game_trace new_trace{};
	interfaces::engine_trace->clip_ray_to_entity({ src, dst }, shot_player, player, &new_trace);

	if (trace.fraction > new_trace.fraction)
		trace = new_trace;
}

bool c_auto_wall::handle_bullet_penetration(
	c_csplayer* const shooter, const weapon_info_t* const wpn_data, const c_game_trace& enter_trace, vector3d& src, const vector3d& dir, int& pen_count, float& cur_dmg, const float pen_modifier)
{
	const auto enter_surface_data = interfaces::phys_surface_props->get_surface_data(enter_trace.surface.surface_props);

	int enter_material = enter_surface_data->game.material;

	bool solid_surf = ((enter_trace.contents >> 3) & contents_solid);
	bool light_surf = ((enter_trace.surface.flags >> 7) & surf_light);
	bool contentes_grate = enter_trace.contents & contents_grate;
	bool draw_surf = !!(enter_trace.surface.flags & (surf_nodraw));

	if (pen_count == 0 &&
		!contentes_grate &&
		!draw_surf &&
		enter_material != char_tex_grate &&
		enter_material != char_tex_glass)
		return false;

	if (wpn_data->penetration <= 0.f || pen_count == 0)
		return false;

	c_game_trace exit_trace = { };
	if (!trace_to_exit(enter_trace.end, dir, enter_trace, exit_trace, shooter))
	{
		if ((interfaces::engine_trace->get_point_contents(enter_trace.end, mask_shot_hull) & mask_shot_hull) == 0)
			return false;
	}

	const auto exit_surface_data = interfaces::phys_surface_props->get_surface_data(exit_trace.surface.surface_props);
	if (!exit_surface_data)
		return false;

	const float enter_penetration_modifier = enter_surface_data->game.penetration_modifier;
	const float exit_penetration_modifier = exit_surface_data->game.penetration_modifier;
	const float exit_damage_modifier = exit_surface_data->game.penetration_modifier;

	const int exit_material = exit_surface_data->game.material;

	float damage_modifier = 0.16f;
	float penetration_modifier = 0.f;

	if (enter_material == char_tex_grate || enter_material == char_tex_glass)
	{
		damage_modifier = 0.05f;
		penetration_modifier = 3.0f;
	}
	else if (solid_surf || light_surf)
	{
		damage_modifier = 0.16f;
		penetration_modifier = 1.0f;
	}
	else if (enter_material == char_tex_flesh
		&& (((c_csplayer*)enter_trace.entity)->team() == shooter->team())
		&& cvars::ff_damage_reduction_bullets->get_float() >= 0.f)
	{
		if (cvars::ff_damage_bullet_penetration->get_float() == 0.f)
			return false;

		penetration_modifier = cvars::ff_damage_bullet_penetration->get_float();
		damage_modifier = 0.16f;
	}
	else
	{
		damage_modifier = 0.16f;
		penetration_modifier = (enter_penetration_modifier + exit_penetration_modifier) * 0.5f;
	}

	if (enter_material == exit_material)
	{
		if (exit_material == char_tex_wood || exit_material == char_tex_cardboard)
			penetration_modifier = 3.f;
		else if (exit_material == char_tex_plastic)
			penetration_modifier = 2.f;
	}

	float trace_dist = (exit_trace.end - enter_trace.end).length_sqr();
	float pen_mod = std::max(0.f, 1.f / penetration_modifier);

	float dmg_chunk = cur_dmg * damage_modifier;
	float pen_weapon_mod = dmg_chunk + std::max(0.f, (3.f / penetration_modifier) * 1.25f) * (pen_mod * 3.f);

	float lost_dmg_object = ((pen_mod * (trace_dist)) / 24);
	float lost_damage = pen_weapon_mod + lost_dmg_object;

	if (lost_damage > cur_dmg)
		return false;

	if (lost_damage > 0.f)
		cur_dmg -= lost_damage;

	if (cur_dmg < 1.0f)
		return false;

	src = exit_trace.end;
	--pen_count;

	return true;
}

bool is_armored(c_csplayer* player, const int hitgroup)
{
	bool is_armored = false;

	if (player->armor_value() > 0)
	{
		switch (hitgroup)
		{
		case hitgroup_generic:
		case hitgroup_chest:
		case hitgroup_stomach:
		case hitgroup_leftarm:
		case hitgroup_rightarm:
		case hitgroup_neck:
			is_armored = true;
			break;
		case hitgroup_head:
			if (player->has_helmet())
				is_armored = true;
			[[fallthrough]];
		case hitgroup_leftleg:
		case hitgroup_rightleg:
			if (player->has_heavy_armor())
				is_armored = true;
			break;
		default:
			break;
		}
	}

	return is_armored;
}

void c_auto_wall::scale_dmg(c_csplayer* const player, float& dmg, const float weapon_armor_ratio, const float headshot_mult, const int hitgroup)
{
	const bool has_heavy_armor = player->has_heavy_armor();

	float head_damage_scale = player->team() == 3
		? cvars::mp_damage_scale_ct_head->get_float() : player->team() == 2
		? cvars::mp_damage_scale_t_head->get_float() : 1.0f;

	const float body_damage_scale = player->team() == 3
		? cvars::mp_damage_scale_ct_body->get_float() : player->team() == 2
		? cvars::mp_damage_scale_t_body->get_float() : 1.0f;

	if (has_heavy_armor)
		head_damage_scale *= 0.5f;

	switch (hitgroup)
	{
	case hitgroup_head:
		dmg *= headshot_mult * head_damage_scale;
		break;
	case hitgroup_chest:
	case hitgroup_leftarm:
	case hitgroup_rightarm:
	case hitgroup_neck:
	case hitgroup_gear:
		dmg *= body_damage_scale;
		break;
	case hitgroup_stomach:
		dmg *= 1.25f * body_damage_scale;
		break;
	case hitgroup_leftleg:
	case hitgroup_rightleg:
		dmg *= 0.75f * body_damage_scale;
		break;
	default:
		break;
	}

	if (is_armored(player, hitgroup))
	{
		const int armor = player->armor_value();
		float heavy_armor_bonus = 1.0f, armor_bonus = 0.5f, armor_ratio = weapon_armor_ratio * 0.5f;

		if (has_heavy_armor)
		{
			heavy_armor_bonus = 0.25f;
			armor_bonus = 0.33f;
			armor_ratio *= 0.20f;
		}

		float damage_to_health = dmg * armor_ratio;
		const float damage_to_armor = (dmg - damage_to_health) * (heavy_armor_bonus * armor_bonus);

		if (damage_to_armor > static_cast<float>(armor))
			damage_to_health = dmg - static_cast<float>(armor) / armor_bonus;

		dmg = damage_to_health;
	}
}

pen_data_t c_auto_wall::fire_bullet(c_csplayer* const shooter, c_csplayer* const target, const weapon_info_t* const wpn_data, const bool is_taser, vector3d src, const vector3d& dst, bool ignore_target, c_game_trace* tr)
{
	auto pen_modifier = std::max((3.f / wpn_data->penetration) * 1.25f, 0.f);

	float cur_dist{};
	pen_data_t data{};

	data.remaining_pen = 4;

	auto cur_dmg = static_cast<float>(wpn_data->dmg);
	auto dir = dst - src;
	dir = dir.normalized();

	c_game_trace trace{};
	c_trace_filter trace_filter{};
	trace_filter.skip = shooter;

	auto max_dist = wpn_data->range;

	while (cur_dmg > 0.f)
	{
		max_dist -= cur_dist;

		const auto cur_dst = src + dir * max_dist;

		interfaces::engine_trace->trace_ray(ray_t(src, cur_dst), shot_player, &trace_filter, &trace);

		if (target)
			clip_trace_to_player(src, cur_dst + dir * 40.f, trace, target, nullptr);
		else
			clip_trace_to_players(src, cur_dst + dir * 40.f, shot_player, &trace_filter, &trace, nullptr);

		if (trace.fraction == 1.f)
			break;

		cur_dist += trace.fraction * max_dist;
		cur_dmg *= std::pow(wpn_data->range_modifier, cur_dist / 500.f);

		const auto enter_surface = interfaces::phys_surface_props->get_surface_data(trace.surface.surface_props);

		if (cur_dist > 3000.f || enter_surface->game.penetration_modifier < 0.1f)
			break;

		if (target)
		{
			if (tr)
				*tr = trace;

			if (trace.entity)
			{
				const auto is_player = trace.entity->is_player();
				if ((trace.entity == target || (is_player && ((c_csplayer*)trace.entity)->team() != shooter->team())) && trace.hitgroup != hitgroup_generic && trace.hitgroup != hitgroup_gear)
				{
					data.hit_player = (c_csplayer*)trace.entity;
					data.hitbox = trace.hitbox;
					data.hitgroup = trace.hitgroup;

					if (is_taser)
						data.hitgroup = 0;

					scale_dmg(data.hit_player, cur_dmg, wpn_data->armor_ratio, wpn_data->crosshair_delta_distance, data.hitgroup);

					data.dmg = static_cast<int>(cur_dmg);

					return data;
				}
			}
		}

		if (is_taser)
			break;

		if (!handle_bullet_penetration(shooter, wpn_data, trace, src, dir, data.remaining_pen, cur_dmg, pen_modifier))
			break;

		if (ignore_target)
			data.dmg = static_cast<int>(cur_dmg);
	}

	return data;
}