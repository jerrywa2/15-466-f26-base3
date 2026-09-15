#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <algorithm>
#include <cassert>
#include <cmath>

GLuint main_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > main_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("player.pnct"));
	main_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});
 
GLuint obstacle_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > obstacle_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("obstacle.pnct"));
	obstacle_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});
 
Load< Scene > main_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("player.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = main_meshes->lookup(mesh_name);
		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();
		drawable.pipeline = lit_color_texture_program_pipeline;
		drawable.pipeline.vao = main_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
	});
});


// Load< Sound::Sample > dusty_floor_sample(LoadTagDefault, []() -> Sound::Sample const * {
// 	return new Sound::Sample(data_path("dusty-floor.opus"));
// });


// Load< Sound::Sample > honk_sample(LoadTagDefault, []() -> Sound::Sample const * {
// 	return new Sound::Sample(data_path("honk.wav"));
// });

Load< Sound::Sample > music_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("track.wav"));
});

float PlayMode::beat_error(float t) {
	float phase = std::fmod(t, period);
	if (phase < 0.0f) phase += period; //fmod keeps the sign of t
	return std::min(phase, period - phase);
}

PlayMode::PlayMode() : scene(*main_scene) {

	for (auto &transform : scene.transforms) {
		if (transform.name == "Player") player = &transform;
	}
	if (player == nullptr) throw std::runtime_error("Player not found.");

	ground_z = player->position.z;

	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	music_loop = Sound::loop(*music_sample, 1.0f, 0.0f);
	song_timestamp = 0.0f;
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_SPACE) {
			if (!evt.key.repeat) space_pressed = true;
			return true;
		}
	}
	return false;

}
 
void PlayMode::advance_chart() {
	static std::mt19937 rng(std::random_device{}());
 
	//An obstacle of tier k can only be cleared by a k-beat jump
	uint32_t gap = next_tier + 1u + (rng() % 2u);
	next_arrival_beat += int64_t(gap);
 
	if (next_tier < max_combo && score >= 4u * next_tier) next_tier += 1u;
}


void PlayMode::update(float elapsed) {

	song_timestamp += elapsed;

	if (space_pressed) {
		space_pressed = false;
		buffer_left = jump_buffer;
		buffered_grid_time = grid_time();
	}

	//jump
	if (buffer_left > 0.0f) {
		buffer_left -= elapsed;
 
		if (!game_over && on_ground && player != nullptr) {
			buffer_left = 0.0f;
 
			bool hit = (beat_error(buffered_grid_time) <= beat_leniency);
			if (hit) {
				combo = std::min(combo + 1u, max_combo);
				feedback_text = "ON BEAT  x" + std::to_string(combo);
			} else {
				combo = 1u; //still jumps, but drops back to the base hop
				feedback_text = "OFF BEAT";
			}
			feedback_timer = 0.5f;
 
			airborne_beats = combo;
			player_vz = jump_speed(airborne_beats);
			on_ground = false;
		}
	}

	//player physics
	if (!game_over && player != nullptr && !on_ground) {
		player_vz -= Gravity * elapsed;
		player->position.z += player_vz * elapsed;
		if (player_vz <= 0.0f && player->position.z <= ground_z) {
			player->position.z = ground_z;
			player_vz = 0.0f;
			on_ground = true;
		}
	}

	while (!game_over) {
		float arrival = (float(next_arrival_beat) + 0.5f) * period; //off-beat!
		if (arrival - SpawnLead > grid_time()) break;
 
		//get spawn distance from the time actually remaining
		float distance = (arrival - grid_time()) * ObstacleSpeed;
		float h = tier_height(next_tier);
 
		spawn(*obstacle_meshes, obstacle_meshes_for_lit_color_texture_program,
			"Obstacle", glm::vec3(0.0f, distance, ground_z),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, h));
		spawned.back().velocity = glm::vec3(0.0f, -ObstacleSpeed, 0.0f);
		spawned.back().clear_height = h;
 
		advance_chart();
	}

	std::vector< Scene::Transform * > to_despawn;
 
	//obstacle physics
	for (auto &s : spawned) {
		s.transform->position += s.velocity * elapsed;

		float span = PlayerHalfY + ObstacleHalfY;

		if (!game_over && player != nullptr) {
			if (std::abs(s.transform->position.y - player->position.y) < span
			 && player->position.z - ground_z < s.clear_height) {
				game_over = true;
				combo = 0;
				feedback_text = "";
				if (music_loop) music_loop->stop(1.0f / 60.0f);
				to_despawn.emplace_back(player);
				player = nullptr;
			}
		}

		if (!s.resolved && s.transform->position.y < -span) {
			s.resolved = true;
			if (!game_over) score += 1;
		}

		if (s.transform->position.y < -20.0f) to_despawn.emplace_back(s.transform);
	}
 
	for (auto *t : to_despawn) despawn(t);


}


Scene::Transform *PlayMode::spawn(MeshBuffer const &buffer, GLuint vao,
	std::string const &mesh_name,
	glm::vec3 const &position, glm::quat const &rotation, glm::vec3 const &scale) {

	Mesh const &mesh = buffer.lookup(mesh_name); //throws if name is wrong

	scene.transforms.emplace_back();
	Scene::Transform *transform = &scene.transforms.back();
	transform->name = mesh_name;
	transform->position = position;
	transform->rotation = rotation;
	transform->scale = scale;

	scene.drawables.emplace_back(transform);
	Scene::Drawable &drawable = scene.drawables.back();

	drawable.pipeline = lit_color_texture_program_pipeline;
	drawable.pipeline.vao = vao;
	drawable.pipeline.type = mesh.type;
	drawable.pipeline.start = mesh.start;
	drawable.pipeline.count = mesh.count;

	spawned.emplace_back(SpawnedObject{transform, mesh_name});
	return transform;
}

void PlayMode::despawn(Scene::Transform *transform) {

	if (!transform) return;
	assert(!camera || camera->transform != transform);

	//reparent children
	for (auto &t : scene.transforms) {
		if (t.parent == transform) t.parent = transform->parent;
	}

	//erase anything referring to this transform
	scene.drawables.remove_if([transform](Scene::Drawable const &d){ return d.transform == transform; });
	scene.cameras.remove_if([transform](Scene::Camera const &c){ return c.transform == transform; });
	scene.lights.remove_if([transform](Scene::Light const &l){ return l.transform == transform; });

	//the transform itself
	scene.transforms.remove_if([transform](Scene::Transform const &t){ return &t == transform; });

	spawned.erase(
		std::remove_if(spawned.begin(), spawned.end(),
			[transform](SpawnedObject const &s){ return s.transform == transform; }),
		spawned.end());
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	GL_ERRORS(); //print any errors produced by this setup code

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		constexpr float H = 0.09f;
		lines.draw_text("SPACE TO JUMP",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		lines.draw_text("SCORE: " + std::to_string(score),
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H + 0.2f, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		if (game_over)
		lines.draw_text("GAME OVER",
			glm::vec3(-aspect + 18 * H, -1.0 + 12 * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
	}
}