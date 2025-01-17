#pragma once

#include <list>
#include "marker.hpp"
#include "food.hpp"
#include "world.hpp"
#include "config.hpp"
#include <iostream>
#include "direction.hpp"
#include "number_generator.hpp"


struct Ant
{
	Ant() = default;

	Ant(float x, float y, float angle)
		: position(x, y)
		, direction(angle)
		, last_direction_update(RNGf::getUnder(1.0f) * direction_update_period)
		, last_marker(RNGf::getUnder(1.0f) * marker_period)
		, phase(Marker::Type::ToFood)
		, liberty_coef(RNGf::getRange(0.0001f, 0.001f))
		, hits(0)
		, markers_count(0.0f)
	{
	}

	void update(const float dt, World& world)
	{
		updatePosition(world, dt);
		if (phase == Marker::ToFood) {
			checkFood(world);
		}

		last_direction_update += dt;
		if (last_direction_update > direction_update_period) {
			findMarker(world, dt);
			direction += RNGf::getFullRange(direction_noise_range);
			last_direction_update = 0.0f;
		}

		last_marker += dt;
		if (last_marker >= marker_period) {
			addMarker(world);
		}

		direction.update(dt);
	}

	void updatePosition(World& world, float dt)
	{
		sf::Vector2f v = direction.getVec();
		const sf::Vector2f next_position = position + (dt * move_speed) * v;
		if (!world.grid_walls.isEmpty(next_position)) {
			++hits;
			const sf::Vector2f normal = getWallNormal(world, v);
			positions.push_back(position);
			velocities.push_back(v);
			normals.push_back(normal);
			v.x = (normal.x ? 0.1f * normal.x : v.x);
			v.y = (normal.y ? 0.1f * normal.y : v.y);
			v = getNormalized(v);
			direction.setDirectionNow(v);
		}
		else {
			hits = 0;
		}

		if (hits > 1) {
			// Bad but nothing better for now
			position = Conf::COLONY_POSITION;
		}
		
		position += (dt * move_speed) * v;

		position.x = (position.x < 0.0f || position.x > Conf::WIN_WIDTH) ? Conf::COLONY_POSITION.x : position.x;
		position.y = (position.y < 0.0f || position.y > Conf::WIN_HEIGHT) ? Conf::COLONY_POSITION.y : position.y;
	}

	void checkFood(World& world)
	{
		const std::list<Food*> food_spots = world.grid_food.getAllAt(position);
		for (Food* fp : food_spots) {
			if (getLength(position - fp->position) < fp->radius) {
				phase = Marker::ToHome;
				direction.addNow(PI);
				fp->pick();
				markers_count = 0.0f;
				return;
			}
		}
	}

	void checkColony(const sf::Vector2f colony_position)
	{
		if (getLength(position - colony_position) < colony_size) {
			if (phase == Marker::ToHome) {
				phase = Marker::ToFood;
				direction.addNow(PI);
			}
			markers_count = 0.0f;
		}
	}

	void findMarker(World& world, float dt)
	{
		// Init
		const sf::Vector2f dir_vec = direction.getVec();
		const uint32_t grid_cell_size = world.markers.cell_size;
		const float radius = 32.0f;
		const int32_t radius_cell = to<int32_t>(radius / grid_cell_size);
		const int32_t cell_x = to<int32_t>(position.x / grid_cell_size);
		const int32_t cell_y = to<int32_t>(position.y / grid_cell_size);
		const int32_t min_range_x = std::max(1, cell_x - radius_cell);
		const int32_t min_range_y = std::max(1, cell_y - radius_cell);
		const int32_t max_range_x = std::min(int32_t(world.markers.size_width) - 2, cell_x + radius_cell);
		const int32_t max_range_y = std::min(int32_t(world.markers.size_height) - 2, cell_y + radius_cell);
		
		const float cell_size_f = to<float>(grid_cell_size);

		// Sample the markers
		float max_intensity = 0.0f;
		sf::Vector2f max_direction;
		MarkersGrid::Cell* max_cell = nullptr;
		const uint32_t sample_count = 64;
		for (uint32_t i(0); i < sample_count; ++i) {
			const uint32_t sample_x = RNGu32::getRange(min_range_x, max_range_x);
			const uint32_t sample_y = RNGu32::getRange(min_range_y, max_range_y);
			const sf::Vector2f marker_pos = cell_size_f * sf::Vector2f(to<float>(sample_x), to<float>(sample_y)) + cell_size_f * sf::Vector2f(RNGf::get(), RNGf::get());
			sf::Vector2f to_marker = marker_pos - position;
			const float length = getLength(to_marker);
			to_marker = to_marker / length;

			auto& cell = world.markers.getCell(sample_x, sample_y);
			if (length < marker_detection_max_dist) {
				if (dot(to_marker, dir_vec) > 0.3f) {
					// Check for food or colony
					if (cell.permanent[phase]) {
						max_direction = to_marker;
						break;
					}
					// Check for the most intense marker
					if (cell.intensity[phase] > max_intensity) {
						max_intensity = cell.intensity[phase];
						max_direction = to_marker;
						max_cell = &cell;
					}
					// Randomly choose own path
					if (RNGf::proba(liberty_coef)) {
						break;
					}
				}
			}
		}
		// Update direction
		if (max_intensity) {
			if (RNGf::proba(0.3f)) {
				max_cell->intensity[phase] *= 0.99f;
			}
			direction = getAngle(max_direction);
		}
	}

	void addMarker(World& world)
	{
		markers_count += marker_period;
		const float coef = 0.01f;
		const float intensity = 1000.0f * exp(-coef * markers_count);
		world.addMarker(Marker(position, phase == Marker::ToFood ? Marker::ToHome : Marker::ToFood, intensity));
		last_marker = 0.0f;
	}

	void render_food(sf::RenderTarget& target, const sf::RenderStates& states) const
	{
		if (phase == Marker::ToHome) {
			const float radius = 2.0f;
			sf::CircleShape circle(radius);
			circle.setOrigin(radius, radius);
			circle.setPosition(position + length * 0.65f * direction.getVec());
			circle.setFillColor(Conf::FOOD_COLOR);
			target.draw(circle, states);
		}
	}

	sf::Vector2f getWallNormal(World& world, const sf::Vector2f& v) const
	{
		const float cell_size_f = to<float>(world.grid_walls.cell_size);
		const float inv_direction[]{ 1.0f / v.x, 1.0f / v.y };
		const float t_d[]{ std::abs(cell_size_f * inv_direction[0]), std::abs(cell_size_f * inv_direction[1]) };
		const int32_t step[]{v.x >= 0.0f ? 1 : -1, v.y >= 0.0f ? 1 : -1 };
		const sf::Vector2i cell_coords = world.grid_walls.getCellCoords(position);

		float t_max[]{
			((cell_coords.x + (step[0] > 0)) * cell_size_f - position.x) * inv_direction[0],
			((cell_coords.y + (step[1] > 0)) * cell_size_f - position.y) * inv_direction[1]
		};

		if (t_max[0] < t_max[1]) {
			return sf::Vector2f(to<float>(-step[0]), 0.0f);
		}

		return sf::Vector2f(0.0f, to<float>(-step[1]));
	}

	void render_in(sf::VertexArray& va, const uint32_t index) const
	{
		const sf::Vector2f dir_vec(direction.getVec());
		const sf::Vector2f nrm_vec(-dir_vec.y, dir_vec.x);

		va[index + 0].position = position - width * nrm_vec + length * dir_vec;
		va[index + 1].position = position + width * nrm_vec + length * dir_vec;
		va[index + 2].position = position + width * nrm_vec - length * dir_vec;
		va[index + 3].position = position - width * nrm_vec - length * dir_vec;
	}

	// Parameters
	const float width = 3.0f;
	const float length = 4.7f;
	const float move_speed = 50.0f;
	const float marker_detection_max_dist = 40.0f;
	const float direction_update_period = 0.125f;
	const float marker_period = 0.125f;
	const float max_reserve = 100000.0f;
	const float direction_noise_range = PI * 0.1f;
	const float marker_reserve_consumption = 0.01f;
	const float colony_size = 20.0f;

	uint32_t hits;
	std::vector<sf::Vector2f> normals;
	std::vector<sf::Vector2f> positions;
	std::vector<sf::Vector2f> velocities;

	sf::Vector2f position;
	
	Direction direction;

	float last_direction_update;
	float markers_count;
	float last_marker;
	Marker::Type phase;
	float liberty_coef;
	//const uint32_t id;
};
