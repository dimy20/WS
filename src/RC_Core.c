#include "RC_Core.h"

#define RCDEF inline static

static Rc_context * rctx;
static bool initted = false;

RCDEF void RC_Core_clear_buffer(){
	size_t dim = rctx->proj_plane_w * rctx->proj_plane_h;
	memset(rctx->fbuffer, 0, sizeof(uint32_t) * dim);
}

RCDEF double RC_Core_perpendicular_distance(double viewing_angle, const vec2f * p, const vec2f * hit){
	double dx = hit->x - p->x;
	double dy = p->y - hit->y;
	return (dx * cos(TO_RAD(viewing_angle))) + (dy * sin(TO_RAD(viewing_angle)));
}

RCDEF double RC_Core_real_distance(double angle, const vec2f * a, const vec2f * b){
	double dx = fabs(b->x - a->x);
	double dy = fabs(b->y - a->y);
	return sqrt(dx*dx + dy*dy);
}

void RC_Core_init(size_t proj_plane_w, size_t proj_plane_h, double fov, SDL_Surface ** textures, size_t textures_len){
	if(initted){
		//TODO: Log error
		return;
	}
	rctx = malloc(sizeof(Rc_context));
	assert(rctx != NULL);

	memset(rctx, 0, sizeof(Rc_context));

	rctx->proj_plane_w = proj_plane_w;
	rctx->proj_plane_h = proj_plane_h;
	rctx->proj_plane_center = proj_plane_h / 2;

	rctx->hits = malloc(sizeof(vec2f) * rctx->proj_plane_w);
	memset(rctx->hits, 0, sizeof(vec2f) * rctx->proj_plane_w);

	size_t dim = rctx->proj_plane_w * rctx->proj_plane_h;
	rctx->fbuffer = malloc(sizeof(uint32_t) * dim);
	RC_Core_clear_buffer();

	assert(rctx->fbuffer != NULL);
	initted = true;
	rctx->angle_step = fov / (double)rctx->proj_plane_w;

	if(textures != NULL){
		rctx->textures = textures;
		rctx->textures_len = textures_len;

	}
}

void RC_Core_quit(){
	assert(rctx != NULL);
	free(rctx->hits);
	free(rctx->fbuffer);
	free(rctx);
}

static double RC_Core_cast_horizontal_intercept(const double ray_angle,
									  const Player * player,
									  const Map * map, vec2f * h_hit, vec2i * map_coords){
	int step_y;
	double delta_step_x;


	/*Cell grid position for the current player's position.
	 * We actually  this down because we're actually looking for h_hit
	 * x and y positions, not the player's.*/
	int map_y = (int)(player->position.y / map->cell_size);

	if(ray_angle > 0.0 && ray_angle < 180.0){
		h_hit->y = map_y * (map->cell_size) - 1;

		double dy = (player->position.y) - h_hit->y;
		double dx = dy / tan(TO_RAD(ray_angle));

		h_hit->x = (player->position.x) + dx;

		step_y = -map->cell_size;
	}else{
		h_hit->y = (map_y * map->cell_size) + map->cell_size;

		double dy = h_hit->y - (player->position.y);
		double dx = dy / -tan(TO_RAD(ray_angle));

		h_hit->x = (player->position.x) + dx;
		step_y = map->cell_size;
	}

	delta_step_x = (double)map->cell_size / tan(TO_RAD(ray_angle));
	delta_step_x = (ray_angle > 180.0) ? -delta_step_x : delta_step_x;

	double distance = DBL_MAX;

	bool hit = false;
	if(ray_angle != 0 && ray_angle != 180){
		while(!hit){
			int x = (int)(h_hit->x / map->cell_size);
			int y = (int)(h_hit->y / map->cell_size);
			if(x >= map->w || x < 0 || y >= map->h || y < 0){
				map_coords->x = INT_MAX;
				map_coords->y = INT_MAX;
				break;
			}else if(map->values[y * map->w + x] & WALL_BIT){

				map_coords->x = x;
				map_coords->y = y;
				hit = true;
				distance = RC_Core_perpendicular_distance(player->viewing_angle, &player->position, h_hit);
			}else{
				h_hit->x += delta_step_x;
				h_hit->y += (double)step_y;
			}
		}
	}

	return distance;
}

static double RC_Core_cast_vertical_intercept(double ray_angle, const Player * player, const Map * map, vec2f * v_hit, vec2i * map_coords){
	int step_x;
	double delta_step_y;

	// U, R
	int map_x = (int)((player->position.x) / map->cell_size);
	if(ray_angle < 90.0 || ray_angle > 270.0){
		v_hit->x = (map_x * map->cell_size) + map->cell_size;
		//NOTE: floating point convesion, careful?
		double dx = v_hit->x - (player->position.x);
		double dy = (double)dx * tan(TO_RAD(ray_angle));
		v_hit->y = (player->position.y) - dy;

		step_x = map->cell_size;
		delta_step_y = -(step_x * tan(TO_RAD(ray_angle)));
	}else{
		v_hit->x = (map_x * map->cell_size) -1;
		double dx = (player->position.x) - v_hit->x;

		double dy = -(dx * tan(TO_RAD(ray_angle)));
		v_hit->y = (player->position.y) - dy;

		step_x = -map->cell_size;
		delta_step_y = -(step_x * tan(TO_RAD(ray_angle)));
	}

	double distance = DBL_MAX;

	bool hit = false;
	if(ray_angle != 180.0 && ray_angle != 90.0){
		while(!hit){
			int x = v_hit->x / map->cell_size;
			int y = v_hit->y / map->cell_size;
			if(x >= map->w || x < 0 || y >= map->h || y < 0){
				map_coords->x = INT_MAX;
				map_coords->y = INT_MAX;
				break;
			}else if(map->values[y * map->w + x] & WALL_BIT){
				map_coords->x = x;
				map_coords->y = y;
				distance = RC_Core_perpendicular_distance(player->viewing_angle, &player->position, v_hit);
				hit = true;
			}else{
				v_hit->x += (double)step_x;
				v_hit->y += delta_step_y;
			}
		}
	}

	return distance;
}

RCDEF void draw_wall_slice(int y_top, int y_bot, int x, uint32_t color){
	if(y_top < 0)
		y_top = 0;

	if(y_bot >= rctx->proj_plane_h)
		y_bot = rctx->proj_plane_h;

	for(int y = y_top; y < y_bot; y++){

		assert(x >= 0 && x < rctx->proj_plane_w);
		assert(y >= 0 && y < rctx->proj_plane_h);

		rctx->fbuffer[y * rctx->proj_plane_w + x] = color;
	}
}

/*Draws a texture mapped wall slice for the current x value*/
void RC_Core_draw_textmapped_wall_slice(int texture_x, int slice_height, int screen_x, SDL_Surface * texture){
	assert(texture != NULL);

	int pixel_y;
	int texture_y;

	size_t texture_size = texture->w;

	for(int i = 0; i < slice_height; i++){
		// location of this texture pixel on screen?
		pixel_y = i + (rctx->proj_plane_center - slice_height / 2);
		if(pixel_y >= 0 && pixel_y < rctx->proj_plane_h){

			/*Makes the following mapping of values from [0, size] -> [0, column_height]
			*Scaling the original texture to column height*/
			texture_y = ((i * texture_size) / slice_height);
			uint32_t pixel = ((uint32_t *)texture->pixels)[texture_y * texture->w + texture_x];
			rctx->fbuffer[pixel_y * rctx->proj_plane_w + screen_x] = pixel;
		}
	}
}
/*
 * When drawing a wall slice is finished, We can draw the corresponding  slice for the 
 * previously
 * rendered wall slice. The process in this case will be reversed, instead of casting rays 
 * in world space, finding the distance to wall slicees and corresponding height in screen space,
 * will be finding a position in world space from a position in screen space.
 * (screen_x, y) -> point in world space, we'll map this point to a  texture pixel color
 * and use that color to draw the (screen_x, y) pixel on the screen.
 *  y E [wall_slice_bottom_y, plane_height]
 *
 *  Using similar triangle equation and some trig we find all the values we need.
 * */
inline static void RC_Core_draw_floor_slice(const Player * player, const Map * map, double ray_angle,
							 int screen_x, int wall_bottom_y){

	assert(player != NULL);
	assert(map != NULL);
	assert(rctx->textures != NULL);

	vec2f P;
	double straight_dist_to_P; // the straight distance to the  point P.

	// ray is assumed normalized
	double ray_dir_x = cos(TO_RAD(ray_angle));
	double ray_dir_y = -sin(TO_RAD(ray_angle));

	double beta = player->viewing_angle - ray_angle;
	double cosine_beta = cos(TO_RAD(beta));

	for(int y = wall_bottom_y; y < rctx->proj_plane_h; y++){ // the range mentioned above
		int row_diff = y - rctx->proj_plane_center;
		// from similar triangle we can find the perpendicular distance from player to P.
		straight_dist_to_P = ((double)player->height / (double)row_diff) * player->dist_from_proj_plane;

		/* We can derive this by looking a the scene from a top down perspective.
		   After finding the real distace we can just scale the ray by this value
		   to find p. */

		double real_distance_to_P = straight_dist_to_P / cosine_beta;

		P.x = player->position.x + (ray_dir_x * real_distance_to_P);
		P.y = player->position.y + (ray_dir_y * real_distance_to_P);

		int map_x = P.x / map->cell_size;
		int map_y = P.y / map->cell_size;

		if(map_x >= 0 && map_x < map->w && map_y >= 0 && map_y < map->h){
			int texture_x = (int)P.x % map->cell_size;
			int texture_y = (int)P.y % map->cell_size;

			uint32_t cell_data = map->values[map_y * map->w + map_x];
			if(cell_data & FLOOR_CEIL_BIT){
				int text_index = (int)((cell_data >> 16) & 0xff);

				assert(text_index >= 0 && text_index < rctx->textures_len);

				SDL_Surface * texture = rctx->textures[text_index];

				assert(texture != NULL);
				assert(texture_x >= 0 && texture_x < texture->w &&
					   texture_y >= 0 && texture_y < texture->h);

				uint32_t pixel = ((uint32_t *)texture->pixels)[texture_y * texture->w + texture_x];
				rctx->fbuffer[y * rctx->proj_plane_w + screen_x] = pixel;

			}

		}
	}

}

/* 
 * This function is symetric to the  slice drawing function.
 * It will draw the corresponding ceiling slice for a given column screen_x.
 * The process of finding the world point P in the ceiling is completley symetric 
 * to process of findig a point P for a  cast. both  and celing drawing could be
 * merged into a single function, however since this raycasting engine will have vertical
 * movement and possible flying, it's better to keep them seperate.
 * */
inline static void RC_Core_draw_celing_slice(const Player * player, const Map * map, double ray_angle, 
								      int screen_x, int wall_top){

	double straight_dist_to_P;
	double real_dist_to_P;

	double beta = ray_angle - player->viewing_angle;
	double cosine_beta = cos(TO_RAD(beta));

	double ray_dir_x = cos(TO_RAD(ray_angle));
	double ray_dir_y = -sin(TO_RAD(ray_angle));

	vec2f P;
	for(int y = wall_top; y >= 0; y--){
		int row_diff = rctx->proj_plane_center - y;
		straight_dist_to_P = ((double)player->height / (double)row_diff) * player->dist_from_proj_plane;

		real_dist_to_P = straight_dist_to_P / cosine_beta;

		P.x = player->position.x + (ray_dir_x * real_dist_to_P);
		P.y = player->position.y + (ray_dir_y * real_dist_to_P);

		int map_x = P.x / map->cell_size;
		int map_y = P.y / map->cell_size;

		if(map_x >= 0 && map_x < map->w && map_y >= 0 && map_y < map->h){
			int texture_x = (int)P.x % map->cell_size;
			int texture_y = (int)P.y % map->cell_size;


			uint32_t cell_data = map->values[map_y * map->w + map_x];
			if(cell_data & FLOOR_CEIL_BIT){
				int ceiling_text_i = (cell_data >> 8) & 0xff;
				assert(ceiling_text_i >= 0 && ceiling_text_i < rctx->textures_len);

				SDL_Surface * ceiling_texture = rctx->textures[ceiling_text_i];
				assert(ceiling_texture != NULL);

				assert(texture_x >= 0 && texture_x < ceiling_texture->w &&
					   texture_y >= 0 && texture_y < ceiling_texture->h);

				uint32_t pixel = ((uint32_t *)ceiling_texture->pixels)[texture_y * ceiling_texture->w + texture_x];
				rctx->fbuffer[y * rctx->proj_plane_w + screen_x] = pixel;
			}

		}
	}
}

const uint32_t * RC_Core_render(const Player * player, const Map * map, uint32_t flags){ 
	assert(initted);
	assert(player != NULL);
	assert(map != NULL);

	// move the starting ray_angle direction to the leftmost part of the arc
	double ray_angle = player->viewing_angle + (player->fov * 0.5);

	RC_Core_clear_buffer();
	memset(rctx->hits, 0, sizeof(vec2f) * rctx->proj_plane_w);

	vec2i map_coords_h, map_coords_v;
	vec2f h_hit, v_hit;

	/*Trace a ray for every colum*/
	for(size_t x = 0; x < rctx->proj_plane_w; x++){
		if(ray_angle < 0) ray_angle += 360.0f;

		double h_dist = RC_Core_cast_horizontal_intercept(ray_angle, player, map, &h_hit, &map_coords_h);
		double v_dist = RC_Core_cast_vertical_intercept(ray_angle, player, map, &v_hit, &map_coords_v);

		assert(h_dist > 0 && v_dist > 0);

		rctx->hits[x] = h_dist < v_dist ? h_hit: v_hit;

		int texture_x = h_dist < v_dist ? (int)h_hit.x % 64 : (int)v_hit.y % 64;
		
		vec2i * map_coords = h_dist < v_dist ? &map_coords_h : &map_coords_v;

		double dist_to_wall = MIN(h_dist, v_dist);
		int slice_height = (int)(((double)map->cell_size / dist_to_wall) * player->dist_from_proj_plane);

        int wall_bot = (slice_height * 0.5f) + rctx->proj_plane_center;
	    int wall_top = rctx->proj_plane_center - (slice_height * 0.5f);       

		assert(map_coords->x >= 0     &&
			   map_coords->x < map->w &&
			   map_coords->y >= 0     &&
			   map_coords->y < map->h);


		uint32_t cell_data = map->values[map_coords->y * map->w + map_coords->x];
		assert(cell_data & WALL_BIT);

		uint32_t cell_index = cell_data >> 8;
		uint32_t color = map->colors[cell_index];

		if(flags & DRAW_TEXT_MAPPED_WALLS){
			assert(rctx->textures != NULL && rctx->textures_len > 0);
			if(cell_index < rctx->textures_len){
				RC_Core_draw_textmapped_wall_slice(texture_x, slice_height, x, rctx->textures[cell_index]);
			}
		}

		if(flags & DRAW_RAW_WALLS){
			draw_wall_slice(wall_top, wall_bot, x, color);
		}

		RC_Core_draw_floor_slice(player, map, ray_angle, x, wall_bot);
		RC_Core_draw_celing_slice(player, map, ray_angle, x, wall_top);

		ray_angle -= rctx->angle_step;
		if(ray_angle >= 360.0f) ray_angle -= 360.0f;
	}

	return rctx->fbuffer;
}

const vec2f * RC_Core_hits(){
	return rctx->hits;
}
