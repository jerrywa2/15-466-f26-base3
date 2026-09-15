#pragma once

#include "Mode.hpp"

#include "GL.hpp"
#include "Mesh.hpp"
#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	Scene::Camera *camera = nullptr;
	Scene::Transform *player = nullptr;
	float ground_z = 0.0f;

	//music
	std::shared_ptr< Sound::PlayingSample > music_loop;
	float song_timestamp = 0.0f; //seconds since the track started

	static constexpr float bpm           = 120.0f;
	static constexpr float period        = 60.0f / bpm;
	static constexpr float beat_offset   = 0.0f; //seconds from file start to the first downbeat
	static constexpr float audio_latency = 0.0f;
	static constexpr float beat_leniency = 0.09f;
	static constexpr float jump_buffer   = 0.12f; //how long an early press is held

	float grid_time() const { return song_timestamp - beat_offset - audio_latency; }
	static float beat_error(float t);

	//physics
	static constexpr float Gravity      = 24.0f;
	static constexpr uint32_t max_combo = 4u; //uint32_t, not float -- std::min needs both args the same type

	//obstacles
	static constexpr float ObstacleSpeed = 14.0f; //world units per second, travelling toward -y
	static constexpr float SpawnLead     = 4.0f; //seconds of travel before arrival
	static constexpr float ClearMargin   = 0.80f; //obstacle height as a fraction of the clearance it demands

	static constexpr float PlayerHalfY   = 0.5f;
	static constexpr float ObstacleHalfY = 0.5f;

	//launch speed that keeps the player airborne for exactly `beats` beats:
	static constexpr float jump_speed(uint32_t beats) {
		return float(beats) * Gravity * period * 0.5f;
	}

	static constexpr float jump_height_at(uint32_t total, float beats) {
		return 0.5f * Gravity * period * period * beats * (float(total) - beats);
	}

	static constexpr float guaranteed_clearance(uint32_t total) {
		return jump_height_at(total, 0.5f);
	}

	static constexpr float tier_height(uint32_t tier) {
		return guaranteed_clearance(tier) * ClearMargin;
	}

	//spawned obstacles
	struct SpawnedObject {
		Scene::Transform *transform = nullptr;
		std::string mesh_name;
		glm::vec3 velocity = glm::vec3(0.0f);
		float clear_height = 0.0f; //player must be above this when the obstacle arrives
		bool resolved = false;
	};
	std::vector< SpawnedObject > spawned;

	Scene::Transform *spawn(MeshBuffer const &buffer, GLuint vao,
		std::string const &mesh_name,
		glm::vec3 const &position,
		glm::quat const &rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		glm::vec3 const &scale = glm::vec3(1.0f));
	void despawn(Scene::Transform *transform);

	//player
	float player_vz = 0.0f;
	bool on_ground = true;
	uint32_t combo = 0;
	uint32_t airborne_beats = 0;

	bool space_pressed = false;
	float buffer_left = 0.0f;
	float buffered_grid_time = 0.0f;

	//chart
	int64_t next_arrival_beat = 8; //obstacles arrive at (beat + 0.5)
	uint32_t next_tier = 1;
	void advance_chart();

	//ui
	uint32_t score = 0;
	bool game_over = false;
	std::string feedback_text;
	float feedback_timer = 0.0f;
};