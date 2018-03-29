#include "Game.h"
#include "Sprite.h"
#include "Button.h"
#include "Text_Form.h"
#include "Connection.h"
#include "FPS_Counter.h"

#include <stdlib.h>
#include <assert.h>

#define gc(T) T __attribute__((__cleanup__(T##_dtor)))
#define new(T, ...) T##_ctor(malloc(sizeof(T)), __VA_ARGS__)
#define range(i, v) size_t i = v - 1; i < v; i--

#undef main
struct Game* game;
struct Connection* connection;
TTF_Font* font;

enum Server_Message
{
	SERVER_DEAD,
	SERVER_WAITING,
	SERVER_POSITION,
	SERVER_SHOOTING,
	SERVER_STARTING,
};

struct Server_Packet
{
	Uint8 type;
	Point data;
};

void player_behavior(Entity* entity)
{
	Sprite* self = (Sprite*)entity;
	int x = self->pos.x;
	int speed = 6;
	if(game->key_state[SDL_SCANCODE_A])
	{
		entity->pos.x -= speed;
		self->selected_anim = 1;
		self->flip_x = 1;
	}
	if(game->key_state[SDL_SCANCODE_D])
	{
		entity->pos.x += speed;
		self->selected_anim = 1;
		self->flip_x = 0;
	}

	if(x == self->pos.x)
		self->selected_anim = 0;

	Sprite_update(entity);
}

void game_scene_update(Scene* self)
{
	static int gravity = 0;
	if(gravity < 15)
		gravity++;

	Sprite* player = (Sprite*)self->content.buffer[0];
	player->pos.y += gravity;

	for(size_t i = 1; i < self->content.size; i++)
	{
		Entity* wall = self->content.buffer[i];
		SDL_Rect p_rect = {player->pos.x + 8, player->pos.y + 6, player->width - 8, player->height - 6};
		SDL_Rect w_rect = {wall->pos.x, wall->pos.y, wall->width, wall->height};
		SDL_Rect result;

		if(SDL_IntersectRect(&p_rect, &w_rect, &result))
		{
			if(result.w > result.h)
			{
				if(result.y > wall->pos.y + wall->height / 2)
				{
					player->pos.y = wall->pos.y + wall->height;
				}
				else
				{
					puts("HAI");
					player->pos.y = wall->pos.y - player->height;
					gravity = 0;
				}
			}
			else
			{
				if(result.x > wall->pos.x + wall->width / 2)
				{
					player->pos.x = wall->pos.x + wall->width;
				}
				else
				{
					player->pos.x = wall->pos.x - player->width;
				}

			}
		}
	}

	if(/*gravity == 0 && */game->key_state[SDL_SCANCODE_W])
	{
		puts("JUMP");
		gravity = -12;
	}

	struct Server_Packet packet;
	if(Connection_incoming_data(connection))
	{
		Connection_recieve(connection, &packet, sizeof(packet));
		switch(packet.type)
		{
		case SERVER_WAITING:
			Scene_dtor(self);
			Vec_pop(&game->scenes);
			return;
		case SERVER_POSITION:;
			Sprite* enemy = (Sprite*)self->content.buffer[1];
			Point pos = packet.data;
			if(pos.x < enemy->pos.x)
			{
				enemy->flip_x = 1;
				enemy->selected_anim = 1;
			}
			else if(pos.x > enemy->pos.x)
			{
				enemy->flip_x = 0;
				enemy->selected_anim = 1;
			}
			else
				enemy->selected_anim = 0;
			self->content.buffer[1]->pos = pos;
			break;
		default:
			puts("Got something");
		}
	}

	static Point pos = {0, 0};
	if(pos.x != self->content.buffer[0]->pos.x && pos.y != self->content.buffer[0]->pos.y)
	{
		packet.type = SERVER_POSITION;
		packet.data = self->content.buffer[0]->pos;
		Connection_send(connection, &packet, sizeof(packet));
	}
}

void game_scene_clean_up(Scene* self)
{
	Texture_dtor(self->content.buffer[0]->texture);
	Texture_dtor(self->content.buffer[1]->texture);
	free(self->content.buffer[0]->texture);
	free(self->content.buffer[1]->texture);

	Sprite_dtor((Sprite*)self->content.buffer[0]);
	Sprite_dtor((Sprite*)self->content.buffer[1]);
	FPS_Counter_dtor((FPS_Counter*)self->content.buffer[2]);
	free(self->content.buffer[0]);
	free(self->content.buffer[1]);
	free(self->content.buffer[2]);

	puts("Rekt");
}

Scene* game_scene_create(void)
{
	Scene* self = new(Scene, 
			NULL, 
			game_scene_update,
			game_scene_clean_up);
	Point walk_raw[] = {
		{0, 0},
		{40, 0},
		{80, 0},
		{120, 0},
	};
	Point stand_raw[] = {
		{160, 0},
		{200, 0}
	};
	Animation walk, stand;
	Animation_ctor(&walk, 100, walk_raw, 4);
	Animation_ctor(&stand, 200, stand_raw, 2);

	Texture* texture = Texture_from_image(malloc(sizeof(Texture)), game->renderer, "Player.png");
	Sprite* player = new(Sprite, texture,
				REG_POINT_TOP_LEFT,
				(Point){
					300, 100
				},
				texture->width / 6,
				texture->height);
	player->behavior = player_behavior;
	Sprite_add(player, stand);
	Sprite_add(player, walk);
	Scene_add(self, (Entity*)player);

	Texture* texture_2 = Texture_copy(texture, malloc(sizeof(Texture)), game->renderer);
	Sprite* enemy = new(Sprite, texture_2,
				REG_POINT_TOP_LEFT,
				(Point){0, 0},
				texture->width / 6,
				texture->height);
	Sprite_add(enemy, stand);
	Sprite_add(enemy, walk);
	Scene_add(self, (Entity*)enemy);

	Scene_add(self, (Entity*)new(FPS_Counter,
				REG_POINT_TOP_RIGHT,
				(Point){game->width - 10, 10},
				font));
	SDL_SetTextureColorMod(texture->raw, 0, 0, 255);
	SDL_SetTextureColorMod(texture_2->raw, 255, 0, 0);

	Texture* ground = Texture_from_image(malloc(sizeof(Texture)), game->renderer, "Ground.png");
	static int map[10][10] = {
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{1, 0, 0, 0, 0, 1, 0, 0, 0, 1},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 1, 0, 0},		
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	};
	for(int i = 0; i < 10; i++)
		for(int j = 0; j < 10; j++)
		{
			if(map[i][j])
				Scene_add(self, new(Entity,
							ground,
							REG_POINT_TOP_LEFT,
							(Point){j * 80, i * 60},
							80, 60,
							NULL));
		}
	return self;
}

char* g_ip;
Uint16 g_port;
void load_scene_update(Scene* self)
{
	static int first = 1;
	if(!first)
	{
		struct Server_Packet packet;
		if(!connection)
		{
			connection = new(Connection,
					g_ip,
					g_port);
			Connection_recieve(connection, &packet, sizeof(packet));
			switch(packet.type)
			{
			case SERVER_WAITING:
				Label_set_text((Label*)self->content.buffer[0], "Waiting for another player...");
				break;
			case SERVER_STARTING:
				puts("Starting game");
				Vec_push(&game->scenes, game_scene_create());
				first = 1;
				return;
			default:
				assert(!"Something weird came from the server");
			}
		}
		else
		{
			if(Connection_incoming_data(connection))
			{
				Connection_recieve(connection, &packet, sizeof(packet));
				switch(packet.type)
				{
				case SERVER_STARTING:
					puts("Starting game");
					Vec_push(&game->scenes, game_scene_create());
					first = 1;
					return;
				default:
					assert(!"Something weird came from the server");
				}
			}
		}
	}
	first = 0;
}

void load_scene_clean_up(Scene* self)
{
	struct Server_Packet packet;
	packet.type = SERVER_WAITING;
	Connection_send(connection, &packet, sizeof(packet));
	Connection_dtor(connection);
	Label_dtor((Label*)self->content.buffer[0]);
	free(self->content.buffer[0]);
	puts("shrekt");
}

Scene* load_scene_create(char* ip, Uint16 port)
{
	Scene* self = new(Scene, 
			NULL, 
			load_scene_update,
			load_scene_clean_up);
	Scene_add(self, (Entity*)new(Label,
				REG_POINT_CENTER,
				(Point){game->width / 2, game->height / 2 - 50},
				font,
				"Trying to connect to server...",
				NULL));
	g_ip = ip;
	g_port = port;
	return self;
}

void menu_scene_content_update(Scene* self, Entity* e)
{
	if(e->name == BUTTON_NAME)
	{
		Button* button = (Button*)e;
		if(button->pressed)
		{
			Text_Form* ip_form = (Text_Form*)self->content.buffer[1];
			Text_Form* port_form = (Text_Form*)self->content.buffer[2];

			if(ip_form->input.size > 1 && port_form->input.size > 1)
			{
				self->done = 1;
				return;
			}
		}
		button->pressed = 0;
	}

	SDL_Rect rect;
	Button* button = (Button*)self->content.buffer[3];
	rect.x = button->pos.x - 10;
	rect.y = button->pos.y - 2;
	rect.w = button->width + 20;
	rect.h = button->height + 2;

	SDL_SetRenderDrawColor(game->renderer, 0, 255, 0, 255);
	SDL_RenderFillRect(game->renderer, &rect);
	SDL_SetRenderDrawColor(game->renderer, 0, 0, 0, 255);
}

void menu_scene_update(Scene* self)
{
	if(self->done)
	{
		Text_Form* ip_form = (Text_Form*)self->content.buffer[1];
		Text_Form* port_form = (Text_Form*)self->content.buffer[2];
		Vec_push(
				&game->scenes, 
				load_scene_create(ip_form->input.buffer, atoi(port_form->input.buffer)));
	}
}

void menu_scene_clean_up(Scene* self)
{
	Label_dtor((Label*)self->content.buffer[0]);
	Text_Form_dtor((Text_Form*)self->content.buffer[1]);
	Text_Form_dtor((Text_Form*)self->content.buffer[2]);
	Button_dtor((Button*)self->content.buffer[3]);

	for(range(i, self->content.size))
		free(self->content.buffer[i]);
}

void title_behavior(Entity* e)
{
	static int should_enlarge = 0;
	if(e->width >= 350)
	{
		should_enlarge = 0;
		SDL_SetTextureColorMod(e->texture->raw, rand() % 256, rand() % 256, rand() % 256);
	}
	else if(e->width <= -200)
	{
		should_enlarge = 1;
		SDL_SetTextureColorMod(e->texture->raw, rand() % 256, rand() % 256, rand() % 256);

	}

	if(should_enlarge)
	{
		e->width += e->texture->width / 60;
		e->height += e->texture->height / 30;
	}
	else
	{
		e->width -= e->texture->width / 60;
		e->height -= e->texture->height / 30;
	}
}

Scene* menu_scene_create(void)
{
	Scene* self = new(Scene, 
			menu_scene_content_update, 
			menu_scene_update,
			menu_scene_clean_up);

	Label* label = new(Label,
				REG_POINT_CENTER,
				(Point){game->width / 2, 135},
				font,
				"Erik's Awesome Game",
				title_behavior);
	Scene_add(self, (Entity*)label);

	Text_Form* ip_form = new(Text_Form,
				REG_POINT_TOP_LEFT,
				(Point){game->width / 2 - 120, 300},
				font,
				"IP: ",
				INPUT_NUM | INPUT_SYM,
				15);
	Scene_add(self, (Entity*)ip_form);

	Text_Form* port_form = new(Text_Form,
				REG_POINT_TOP_LEFT,
				(Point){game->width / 2 - 120, 350},
				font,
				"Port: ",
				INPUT_NUM,
				5);
	Scene_add(self, (Entity*)port_form);
	Scene_add(
			self,
			(Entity*)new(Button,
				REG_POINT_TOP_LEFT,
				(Point){
					port_form->pos.x + port_form->max_width * (port_form->input_max + 2),
					self->content.buffer[2]->pos.y,
				},
				font,
				"Go!"));
	return self;
}

int main(void)
{
	gc(Game) l_game;
	Game_ctor(&l_game, 800, 600);
	game = &l_game;

	font = TTF_OpenFont("./Roboto-Regular.ttf", 28);
	if(!font)
		assert(!"Unable to open font");

	Scene* menu_scene = menu_scene_create();
	Game_add(game, menu_scene);

	while(!game->done)
		Game_update(game);

	for(size_t i = 0; i < game->scenes.size; i++)
	{
		Scene_dtor(game->scenes.buffer[i]);
		free(game->scenes.buffer[i]);
	}
	TTF_CloseFont(font);
}
