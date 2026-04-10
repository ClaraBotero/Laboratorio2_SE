// Lab 2 Clara Botero, SE.
#include <stdio.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_random.h"

#define OFF 0
#define GREEN 1
#define RED 2

#define GAME_WAIT 0
#define GAME_PLAY 1 // Se está jugando.
#define GAME_END 2 // Ganaste (yeyyyyy) o perdiste.


// Botones.
#define BTN_LEFT 16
#define BTN_RIGHT 14

// Pines ESP32.
int filas[6]     = {13, 12, 2, 4, 15, 0};
int col_rojo[6]  = {25, 27, 33, 5, 19, 22};
int col_verde[6] = {26, 17, 32, 18, 21, 23};

int matrix[6][6];

int frodo_x = 0; // Posición actual de Frodo.
bool sauron_eye_open = true; // Ojo de Sauron abierto.
int game_state = GAME_WAIT;
int sauron_eye_timer = 0;
bool win = false;
int idle_timer = 0;
int last_start_x = -1;

// Estado anterior de cada botón para detección de flanco
bool prev_left  = false;
bool prev_right = false;

void clear_matrix(){
    for(int y=0;y<6;y++)
        for(int x=0;x<6;x++)
            matrix[y][x] = OFF;
}

// Configura todos los pines de filas y columnas como salidas
void init_matrix(){
    for(int i=0;i<6;i++){
        gpio_reset_pin(filas[i]);
        gpio_set_direction(filas[i],GPIO_MODE_OUTPUT);
        gpio_set_level(filas[i],0);
    }
    for(int i=0;i<6;i++){
        gpio_reset_pin(col_rojo[i]);
        gpio_set_direction(col_rojo[i],GPIO_MODE_OUTPUT);
        gpio_set_level(col_rojo[i],0);
    }
    for(int i=0;i<6;i++){
        gpio_reset_pin(col_verde[i]);
        gpio_set_direction(col_verde[i],GPIO_MODE_OUTPUT);
        gpio_set_level(col_verde[i],0);
    }
}

// Enciende una fila durante 800us (multiplexado por filas)
void show_row(int r){
    for(int c=0;c<6;c++){
        gpio_set_level(col_rojo[c],0);
        gpio_set_level(col_verde[c],0);
    }
    gpio_set_level(filas[r],1);
    for(int c=0;c<6;c++){
        if(matrix[r][c]==GREEN)
            gpio_set_level(col_verde[c],1);
        if(matrix[r][c]==RED)
            gpio_set_level(col_rojo[c],1);
    }
    esp_rom_delay_us(800);
    gpio_set_level(filas[r],0);
}

void refresh_display(){
    for(int r=0;r<6;r++)
        show_row(r);
}

void draw_frodo(){
    matrix[5][frodo_x] = GREEN;
}

// Dibuja el ojo abierto (5 LEDs) o cerrado (3 LEDs en línea)
void draw_sauron_eye(){
    if(sauron_eye_open){
        matrix[0][2] = RED;
        matrix[1][1] = RED;
        matrix[1][2] = RED;
        matrix[1][3] = RED;
        matrix[2][2] = RED;
    } else {
        matrix[1][1] = RED;
        matrix[1][2] = RED;
        matrix[1][3] = RED;
    }
}

// La pantalla se pone verde si ganas.
void win_screen(){
    clear_matrix();
    for(int y=0;y<6;y++)
        for(int x=0;x<6;x++)
            matrix[y][x] = GREEN;
}

// La pantalla se pone roja si pierdes.
void lose_screen(){
    clear_matrix();
    for(int y=0;y<6;y++)
        for(int x=0;x<6;x++)
            matrix[y][x] = RED;
}

bool btn_left(){  return gpio_get_level(BTN_LEFT)==0;  }
bool btn_right(){ return gpio_get_level(BTN_RIGHT)==0; }

// Alterna el ojo entre abierto (100 ciclos) y cerrado (300 ciclos)
void update_sauron_eye(){
    sauron_eye_timer++;
    if(!sauron_eye_open && sauron_eye_timer>300){
        sauron_eye_open = true;
        sauron_eye_timer = 0;
    }
    if(sauron_eye_open && sauron_eye_timer>100){
        sauron_eye_open = false;
        sauron_eye_timer = 0;
    }
}

void update_game(bool left, bool right){
    if(!sauron_eye_open){
        // El ojo está cerrado: se permite mover a Frodo
        if(left  && frodo_x > 0) frodo_x--;
        if(right && frodo_x < 5) frodo_x++;
        if(left || right) idle_timer = 0;
    } else {
        // El ojo está abierto: cualquier movimiento es derrota
        if(left || right){
            game_state = GAME_END;
            win = false;
            return;
        }
    }

    // Frodo llego al extremo derecho: victoria
    if(frodo_x == 5){
        game_state = GAME_END;
        win = true;
    }
}

void start_game(){
    // Posición inicial aleatoria, sin repetir la anterior y nunca en columna 5
    int new_x;
    do {
        new_x = (int)(esp_random() % 6);
    } while(new_x == last_start_x);
    last_start_x = new_x;
    if(new_x == 5) new_x = 0;

    frodo_x = new_x;
    sauron_eye_open = true;
    sauron_eye_timer = 0;
    win = false;
    idle_timer = 0;
    // Resetea el debounce para no arrastrar estado del juego anterior
    prev_left  = false;
    prev_right = false;
    game_state = GAME_PLAY;
}

void draw_game(){
    clear_matrix();
    draw_sauron_eye();
    draw_frodo();
}

void app_main(){
    init_matrix();

    // Botones con pull-up interno: nivel 0 = presionado
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL<<BTN_LEFT) | (1ULL<<BTN_RIGHT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_cfg);

    start_game();


    while(1){
        // para el antirebote ese
        bool raw_left  = btn_left();
        bool raw_right = btn_right();

        // Debounce por flanco: solo true en el ciclo exacto que se presiona
        bool left  = raw_left  && !prev_left;
        bool right = raw_right && !prev_right;

        // Guarda el estado para comparar en el siguiente ciclo
        prev_left  = raw_left;
        prev_right = raw_right;

        if(game_state == GAME_PLAY){
            update_game(left, right);
            update_sauron_eye();
            draw_game();
            refresh_display();

            // Derrota automatica si el jugador no hace nada en 1000 ciclos (~10s)
            idle_timer++;
            if(idle_timer > 1000){
                game_state = GAME_END;
                win = false;
            }
        }
        else if(game_state == GAME_END){
            if(win) win_screen();
            else    lose_screen();

            // Muestra la pantalla de resultado durante 3 segundo
            uint32_t end_ms = xTaskGetTickCount() * portTICK_PERIOD_MS + 3000;
            while(xTaskGetTickCount() * portTICK_PERIOD_MS < end_ms){
                refresh_display();
            }
            start_game();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}