#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <math.h>

#include <SDL3/SDL.h>

void sdl_error(const char *error_context) {
	char message[1024];

	snprintf(message, sizeof(message), "%s: %s", error_context, SDL_GetError());

	fprintf(stderr, "%s\n", message);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR", message, NULL);
}

struct Board {
	size_t n_dim;
	// dimensions[0] is the least significant value.
	size_t *dimensions;
	struct Board__Cell{
		size_t neighbour_mine_amount;
		bool mine;
		enum Board__Cell__State {
			BOARD__CELL__STATE_COVERED = 0,
			BOARD__CELL__STATE_UNCOVERED,
			BOARD__CELL__STATE_FLAGGED,
			// An empty tile was flagged, used after losing as feedback to the player
			BOARD__CELL__STATE_FLAGGED_NOT_MINE,
		} state;
	} *cells;
};

// Sets the board to undiscovered empty cells. Returns true on success, false on failure
bool board__init(struct Board *board, size_t n_dim, const size_t dimensions[]) {
	if (n_dim == 0) {
		return false;
	}
	board->n_dim = n_dim;

	board->dimensions = malloc(board->n_dim * sizeof(*board->dimensions));
	if (board->dimensions == NULL) {
		return false;
	}
	memcpy(board->dimensions, dimensions, board->n_dim * sizeof(*board->dimensions));

	size_t length = 1;
	for (size_t i = 0; i < n_dim; i++) {
		length *= dimensions[i];
	}
	if (length == 0) {
		return false;
	}

	size_t size = length * sizeof(struct Board__Cell);
	board->cells = malloc(size);
	if (board->cells == NULL) {
		free(board->dimensions);
		return false;
	}
	memset(board->cells, 0, size);

	return true;
}

void board__free(struct Board *board) {
	free(board->dimensions);
	free(board->cells);
}

size_t board__coord_to_index(const struct Board *board, const size_t coord[]) {
	size_t index = 0;
	size_t multiplier = 1;

	for (size_t n = 0; n < board->n_dim; n++) {
		index += coord[n] * multiplier;
		multiplier *= board->dimensions[n];
	}

	return index;
}
void board__index_to_coord(const struct Board *board, size_t index, size_t coord[]);

void board__flatten_coord(const struct Board *board, const size_t coord[], size_t *x_out, size_t *y_out) {
	size_t x = 0, x_mult = 1;
	size_t y = 0, y_mult = 1;

	for (size_t n = 0; n < board->n_dim; n++) {
		if ((n & 1) == 0) {
			x += coord[n] * x_mult;
			x_mult *= board->dimensions[n];
			x_mult += 1;
		} else {
			y += coord[n] * y_mult;
			y_mult *= board->dimensions[n];
			y_mult += 1;
		}
	}

	*x_out = x;
	*y_out = y;
}

// https://stackoverflow.com/a/822361
// Returns a random number in the range [0, n)
int randint(int n) {
	if ((n + 1) == RAND_MAX) {
		return rand();
	} else {
		assert(n <= RAND_MAX);

		int end = (RAND_MAX / n) * n;
		assert(end != 0);

		int v;
		while ((v = rand()) >= end);
		return v % n;
	}
}

// Tries to place mine_amount of mines on the board, returns actual amount of mines placed, does not recompute mine neighbour values
size_t board__randomize(struct Board *board, size_t mine_amount) {
	size_t board_length = 1;
	for (size_t dim = 0; dim < board->n_dim; dim++) {
		board_length *= board->dimensions[dim];
	}
	assert(board_length != 0);

	size_t mines_placed = 0;
	for (size_t mine = 0; mine < mine_amount; mine++) {
		size_t open_spots = 0;
		for (size_t i = 0; i < board_length; i++) {
			if (!board->cells[i].mine) {
				open_spots++;
			}
		}
		if (open_spots == 0) {
			goto end;
		}

		// TODO: randint returns int
		size_t mine_index = randint(open_spots) + 1;
		for (size_t i = 0; i < board_length; i++) {
			if (!board->cells[i].mine) {
				mine_index--;
			}
			if (mine_index == 0) {
				board->cells[i].mine = true;
				mines_placed++;
				break;
			}
		}
	}

end:
	return mines_placed;
}

// Returns false when done, offset should be initialized to 0
bool board__loop_neighbours(const struct Board *board, int offset[], size_t neighbour_coord_ret[], const size_t coord[]) {
	// coord[0] is the least significant number
start:

	offset[0]++;
	for (size_t n = 0; n < board->n_dim; n++) {
		if (offset[n] != 0) {
			goto cont;
		}
	}
	return false;

	bool carry;
cont:
	carry = false;
	for (size_t n = 0; n < board->n_dim; n++) {
		if (carry) {
			offset[n]++;
		}

		if (offset[n] == 2) {
			offset[n] = -1;
			carry = true;
		} else {
			carry = false;
		}
	}

	for (size_t n = 0; n < board->n_dim; n++) {
		if (offset[n] == -1 && coord[n] == 0) {
			goto start;
		}
		if (offset[n] == 1 && coord[n] == board->dimensions[n] - 1) {
			goto start;
		}

		neighbour_coord_ret[n] = coord[n] + offset[n];
	}

	return true;
}

// Returns false when done, coord should be initialized to 0. Does not yield coord 0, so use it in a do {...} while (...) loop.
// Coord[0] is the least significant value.
bool board__loop_cells(const struct Board *board, size_t coord[]) {
	coord[0]++;

	bool carry = false;
	for (size_t n = 0; n < board->n_dim; n++) {
		if (carry) {
			coord[n]++;
		}

		if (coord[n] == board->dimensions[n]) {
			coord[n] = 0;
			carry = true;
		} else {
			carry = false;
		}
	}

	for (size_t n = 0; n < board->n_dim; n++) {
		if (coord[n] != 0) {
			return true;
		}
	}
	return false;
}

void board__compute_neighbour_values(struct Board *board) {
	size_t center_coord[board->n_dim];
	memset(center_coord, 0, board->n_dim * sizeof(*center_coord));

	do {
		// printf("coord: ");
		// for (size_t n = 0; n < board->n_dim; n++) {
		// 	printf("%zu, ", coord[n]);
		// }
		// printf("\n");

		size_t coord[board->n_dim];
		int offset[board->n_dim];
		memset(offset, 0, board->n_dim * sizeof(*offset));

		size_t neighbour_mine_amount = 0;
		while (board__loop_neighbours(board, offset, coord, center_coord)) {
			size_t index = board__coord_to_index(board, coord);
			struct Board__Cell *cell = &board->cells[index];

			if (cell->mine) {
				neighbour_mine_amount++;
			}
		}

		size_t index = board__coord_to_index(board, center_coord);
		struct Board__Cell *cell = &board->cells[index];
		cell->neighbour_mine_amount = cell->mine ? 0 : neighbour_mine_amount;
	} while(board__loop_cells(board, center_coord));
}

// Used after losing
void board__uncover_all_mines(struct Board *board) {
	size_t coord[board->n_dim];
	memset(coord, 0, board->n_dim * sizeof(*coord));

	do {
		size_t index = board__coord_to_index(board, coord);
		struct Board__Cell *cell = &board->cells[index];

		if (cell->mine && cell->state == BOARD__CELL__STATE_COVERED) {
			cell->state = BOARD__CELL__STATE_UNCOVERED;
		} else if (!cell->mine && cell->state == BOARD__CELL__STATE_FLAGGED) {
			cell->state = BOARD__CELL__STATE_FLAGGED_NOT_MINE;
		}
	} while(board__loop_cells(board, coord));
}

// Recursively uncovers all mines that have zero neighbouring mines
void board__uncover_zero_neighbour_cells(struct Board *board, const size_t coord[]) {
	size_t index = board__coord_to_index(board, coord);
	struct Board__Cell *cell = &board->cells[index];
	if (cell->neighbour_mine_amount != 0) {
		return;
	}

	int offset[board->n_dim];
	memset(offset, 0, board->n_dim * sizeof(*offset));

	size_t neighbour_coord[board->n_dim];

	while (board__loop_neighbours(board, offset, neighbour_coord, coord)) {
		size_t index = board__coord_to_index(board, neighbour_coord);
		struct Board__Cell *cell = &board->cells[index];
		bool cont = cell->state != BOARD__CELL__STATE_UNCOVERED;
		cell->state = BOARD__CELL__STATE_UNCOVERED;

		if (cont) {
			board__uncover_zero_neighbour_cells(board, neighbour_coord);
		}
	}
}

// Returns false if the player lost, true otherwise. Calls board__uncover_all_mines on losing.
bool board__uncover_cell(struct Board *board, const size_t coord[]) {
	size_t index = board__coord_to_index(board, coord);
	struct Board__Cell *cell = &board->cells[index];

	cell->state = BOARD__CELL__STATE_UNCOVERED;

	if (cell->mine) {
		board__uncover_all_mines(board);
		return false;
	} else {
		if (cell->neighbour_mine_amount == 0) {
			board__uncover_zero_neighbour_cells(board, coord);
		}

		return true;
	}
}

#define RENDER_CELL_EMPTY     0
#define RENDER_CELL_FLAGGED 100
#define RENDER_CELL_EXPOSED 101
#define RENDER_CELL_COVERED 102
#define RENDER_CELL_FLAGGED_NOT_MINE 103

// Render a cell, 0 is empty, [1, 99] renders the number, 100 is flagged, 101 is an exposed and exploded mine, 102 is covered, 103 is a cell that was flagged but did not contain a mine.
void render_cell(SDL_Renderer *renderer, SDL_Texture *number_texture, float tile_x, float tile_y, float cell_size, unsigned int number) {
	if (number == RENDER_CELL_EMPTY) {
		return;
	} else if (number == RENDER_CELL_COVERED) {
		SDL_FRect dstrect = {tile_x, tile_y, cell_size, cell_size};

		SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
		SDL_RenderFillRect(renderer, &dstrect);

		SDL_SetRenderDrawColor(renderer, 100, 100, 100, SDL_ALPHA_OPAQUE);
		SDL_RenderRect(renderer, &dstrect);

		return;
	} else if (number >= 100) {
		size_t n;
		if (number == RENDER_CELL_FLAGGED) {
			n = 0;
		} else if (number == RENDER_CELL_EXPOSED) {
			n = 1;
		} else if (number == RENDER_CELL_FLAGGED_NOT_MINE) {
			n = 2;
		} else {
			assert(false && "Unknown render_cell number");
		}

		SDL_FRect srcrect = {50 + n * 10, 0, 10, 10};
		SDL_FRect dstrect = {tile_x, tile_y, cell_size, cell_size};
		SDL_RenderTexture(renderer, number_texture, &srcrect, &dstrect);
		return;
	}

	assert(number < 100);

	div_t v = div(number, 10);

	SDL_FRect dstrect;
	if (v.quot != 0) {
		SDL_FRect srcrect = {5 * v.quot, 0, 5, 10};
		dstrect = (SDL_FRect){tile_x, tile_y, cell_size / 2.0, cell_size};
		SDL_RenderTexture(renderer, number_texture, &srcrect, &dstrect);

		dstrect.x += cell_size / 2.0;
	} else {
		dstrect = (SDL_FRect){tile_x + cell_size / 4.0, tile_y, cell_size / 2.0, cell_size};
	}

	SDL_FRect srcrect = {5 * v.rem, 0, 5, 10};
	SDL_RenderTexture(renderer, number_texture, &srcrect, &dstrect);
}

enum {
	GAME_STATE__PLAYING,
	GAME_STATE__PLAYER_LOST,
	GAME_STATE__PLAYER_WON  // TODO: Actually check if the player won
} game_state = GAME_STATE__PLAYING;

int main() {
	srand(time(NULL));

	if (!SDL_SetAppMetadata("Mines", NULL, "nl.banketbakkerijlaagstaart.mines")) {
		sdl_error("SDL_SetAppMetadata");
		return 1;
	}

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		sdl_error("SDL_Init");
		return 1;
	}

	SDL_Window *window;
	SDL_Renderer *renderer;
	if (!SDL_CreateWindowAndRenderer("mines", 800, 800, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
		sdl_error("SDL_CreateWindowAndRenderer");

		SDL_Quit();
		return 1;
	}

	SDL_Texture *numbers_texture; {
		SDL_Surface *surface = SDL_LoadBMP("numbers.bmp");
		if (surface == NULL) {
			sdl_error("SDL_LoadBMP");

			SDL_DestroyRenderer(renderer);
			SDL_DestroyWindow(window);

			SDL_Quit();
			return 1;
		}

		numbers_texture = SDL_CreateTextureFromSurface(renderer, surface);
		SDL_DestroySurface(surface);
		if (numbers_texture == NULL) {
			sdl_error("SDL_CreateTextureFromSurface");

			SDL_DestroyRenderer(renderer);
			SDL_DestroyWindow(window);

			SDL_Quit();
			return 1;
		}

		SDL_SetTextureScaleMode(numbers_texture, SDL_SCALEMODE_NEAREST);
	}

	const size_t dimensions[] = {4, 4, 4, 4};
	const size_t mine_amount = 10;
	struct Board board; {
		const size_t n_dim = sizeof(dimensions) / sizeof(*dimensions);
		if (!board__init(&board, n_dim, dimensions)) {
			sdl_error("board__init");

			SDL_DestroyRenderer(renderer);
			SDL_DestroyWindow(window);

			SDL_Quit();

			return 1;
		}
	}

	{
		size_t mines_placed = board__randomize(&board, mine_amount);
		printf("Placed %zu mines on the board\n", mines_placed);
	}

	board__compute_neighbour_values(&board);

	bool run = true;
	while (run) {
		SDL_Event event;
		int mouse_button_index = 0;
		bool skip_wait = false;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				run = false;
			} else if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_R) {
					// TODO: This code is duplicated from the initialization code
					{
						const size_t n_dim = sizeof(dimensions) / sizeof(*dimensions);
						if (!board__init(&board, n_dim, dimensions)) {
							sdl_error("board__init");

							SDL_DestroyTexture(numbers_texture);

							SDL_DestroyRenderer(renderer);
							SDL_DestroyWindow(window);

							SDL_Quit();

							return 1;
						}
					}

					{
						size_t mines_placed = board__randomize(&board, mine_amount);
						printf("Placed %zu mines on the board\n", mines_placed);
					}

					board__compute_neighbour_values(&board);

					game_state = GAME_STATE__PLAYING;
				}
			} else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
				mouse_button_index = event.button.button;
			}
		}

		int window_width = 800, window_height = 800;
		SDL_GetWindowSize(window, &window_width, &window_height);

		float mouse_x, mouse_y;
		SDL_MouseButtonFlags mouse_flags = SDL_GetMouseState(&mouse_x, &mouse_y);

		// float cell_size = fmin(window_height, (float)window_width * (16.0 / 9.0)) / 19.0;
		float cell_size = fmin(window_height, window_width) / 19.0;

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
		SDL_RenderClear(renderer);

		size_t hovered_cell_coord[board.n_dim];
		hovered_cell_coord[0] = -1;
		{
			size_t coord[board.n_dim];
			memset(coord, 0, board.n_dim * sizeof(*coord));

			do {
				size_t x, y;
				board__flatten_coord(&board, coord, &x, &y);

				SDL_FRect rect = {x * cell_size, y * cell_size, cell_size, cell_size};
				SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
				SDL_RenderRect(renderer, &rect);

				size_t index = board__coord_to_index(&board, coord);
				struct Board__Cell *cell = &board.cells[index];

				unsigned int number;
				if (!cell->mine && cell->state == BOARD__CELL__STATE_UNCOVERED) {
					number = cell->neighbour_mine_amount;
				} else if (cell->state == BOARD__CELL__STATE_COVERED) {
					number = RENDER_CELL_COVERED;
				} else if (cell->state == BOARD__CELL__STATE_FLAGGED) {
					number = RENDER_CELL_FLAGGED;
				} else if (cell->state == BOARD__CELL__STATE_FLAGGED_NOT_MINE) {
					assert(!cell->mine);
					number = RENDER_CELL_FLAGGED_NOT_MINE;
				} else {
					assert(cell->mine);

					if (cell->state == BOARD__CELL__STATE_UNCOVERED) {
						number = RENDER_CELL_EXPOSED;
					} else {
						assert(false);
					}
				}
				render_cell(renderer, numbers_texture, rect.x, rect.y, cell_size, number);

				if (mouse_x >= rect.x && mouse_y >= rect.y
						&& mouse_x < rect.x + rect.w
						&& mouse_y < rect.y + rect.w) {
					memcpy(hovered_cell_coord, coord, board.n_dim * sizeof(*coord));
				}
			} while (board__loop_cells(&board, coord));
		}

		if (hovered_cell_coord[0] != (size_t)-1 && game_state == GAME_STATE__PLAYING) {
			int offset[board.n_dim];
			memset(offset, 0, board.n_dim * sizeof(*offset));

			size_t coord[board.n_dim];

			// Highlight cells surrounding the currently hovered cell
			while (board__loop_neighbours(&board, offset, coord, hovered_cell_coord)) {
				size_t x, y;
				board__flatten_coord(&board, coord, &x, &y);

				SDL_FRect rect = {x * cell_size, y * cell_size, cell_size, cell_size};
				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
				SDL_SetRenderDrawColor(renderer, 255, 0, 0, 100);
				SDL_RenderFillRect(renderer, &rect);
				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
			}

			if (mouse_button_index == 1) {
				skip_wait = true;

				if (!board__uncover_cell(&board, hovered_cell_coord)) {
					game_state = GAME_STATE__PLAYER_LOST;
					printf("Player lost\n");
				}
			} else if (mouse_button_index == 3) {
				skip_wait = true;

				size_t index = board__coord_to_index(&board, hovered_cell_coord);
				struct Board__Cell *cell = &board.cells[index];

				if (cell->state == BOARD__CELL__STATE_FLAGGED) {
					cell->state = BOARD__CELL__STATE_COVERED;
				} else if (cell->state == BOARD__CELL__STATE_COVERED) {
					cell->state = BOARD__CELL__STATE_FLAGGED;
				}
			}
		}

		SDL_RenderPresent(renderer);

		if (!skip_wait) {
			if (!SDL_WaitEvent(NULL)) {
				sdl_error("SDL_WaitEvent");
				break;
			}
		}
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);

	SDL_Quit();

	return 0;
}

