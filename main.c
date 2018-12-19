//hhggomaa
//jborrome

#include <lpc17xx.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "uart.h"
#include "GLCD.h"
#include <cmsis_os.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define UP 					0x01
#define DOWN 				0x02
#define RIGHT				0x03
#define LEFT				0x04
#define NODIRECTION 0x05

#define WIN         0x01
#define DIE         0x02
#define RESET       0x03

#define WINSCORE    0xFF

typedef uint8_t dir_t;
//default direction of snek
dir_t dir = RIGHT;

bool gameOver = false;
uint8_t gameState = 0;
uint8_t pot_state = 1;

// Dir Mutex protects the direction variable from concurrent reads and writes
osMutexId	dir_mutex;
osMutexDef(dir_mutex);


//linked list structure to store snek pieces
typedef struct node {
    uint8_t  x, y;
    struct node *next;
} node_t;

typedef struct {
	uint8_t x, y;
} food_t;

//global food variable
food_t snekFood;

//snek structure
typedef struct {
	node_t *head;
	uint8_t size;
} snek_t;


// This will only be called on nodes that have moved position
void renderGridSquare(uint16_t x, uint16_t y, uint16_t colour) {
	unsigned short bitmap[100];
	for(uint8_t i = 0; i < 100; i++) {
		bitmap[i] = colour;
	}
	
	// Snake nodes will be represented by a 10px x 10px square on the LCD
	int xMin = 10 * x;
	int yMin = 10 * y;
	
	GLCD_Bitmap(xMin, yMin, 10, 10, (unsigned char*) bitmap);
}

void spawnFood(snek_t *snek) {
	bool openSpaceFound = false;
	uint8_t randX, randY;
	node_t *currNode = snek -> head;
	osMutexWait(dir_mutex, osWaitForever);
	/*
		Keep generating random x,y points until a location that does not touch the snake is found.
		Each point generation and check has an O(n) complexity because each snake node is iterated through.
		This might break as the number of snake nodes increases (both due to the increased likelihood of
		collision and the O(n) iteration.  Luckily, snake size is capped at 255 so this isn't really an 
		issue.  For example, the chance that there is a collision with a size of 255 is 33.2%.  After
		3 iterations, this chance drops to ~3.7%.
	*/
	while(!openSpaceFound) {
		openSpaceFound = true;
		randX = rand() % 32, randY = rand() % 24 ;
		while(currNode !=  NULL) {
			if((currNode -> x == randX && currNode-> y == randY)) {
				openSpaceFound = false;
			}
			currNode = currNode->next;
	 	}
	}
	osMutexRelease(dir_mutex);
	
	//Spawn at randX and randY
	snekFood.x = randX;
	snekFood.y = randY;
	renderGridSquare(snekFood.x, snekFood.y, Red);
}		

void initializeSnek(snek_t *s) {
	// Spawn snake at (15, 11) which is approximately the center and spawn the initial first food.
	s->head = malloc(sizeof(node_t));
	s->head->x = 15;
	s->head->y = 11;
	s->size = 1;
	spawnFood(s);
	renderGridSquare(s->head->x, s->head->y, Black);
}

void destroySnek(snek_t *s) {
	// Deallocate the memory allocated for each snake node (occurs at the end of the game)
	node_t *currNode = s->head;
	while(currNode != NULL) {
		node_t *temp = currNode;
		currNode = currNode -> next;
		free(temp);
	}
}

void displayLED(uint8_t number) {
	// Display the number in binary using the LEDs
	LPC_GPIO1->FIOCLR |= (11 << 28);
	LPC_GPIO1->FIODIR |=  (11 << 28);
	LPC_GPIO2->FIOCLR |= (31 << 2);

	uint8_t r1 = ((number & (7 << 5)) >> 5);

	if (r1 & 1)
			LPC_GPIO1->FIOSET = ((uint32_t)(1)) << 31;
	if((r1 >> 1) & 1)
			LPC_GPIO1->FIOSET = 1 << 29;
	if((r1 >> 2) & 1)
			LPC_GPIO1->FIOSET = 1 << 28;

	uint8_t r2 = (number & 31);
	LPC_GPIO2->FIODIR |= 0x7c;

	if((r2 >> 4) & 1)
			LPC_GPIO2->FIOSET = 1 << 2;
	if((r2 >> 3) & 1)
			LPC_GPIO2->FIOSET = 1 << 3;
	if((r2 >> 2) & 1)
			LPC_GPIO2->FIOSET = 1 << 4;
	if((r2 >> 1) & 1)
			LPC_GPIO2->FIOSET = 1 << 5;
	if(r2 & 1)
			LPC_GPIO2->FIOSET = 1 << 6;
}

uint8_t readJoystick() {
    if(!((LPC_GPIO1->FIOPIN) & 1 << 23)) {
        return UP;
    }
    else if(!((LPC_GPIO1->FIOPIN) & 1 << 24)) {
        return RIGHT;
    }
    else if (!((LPC_GPIO1->FIOPIN) & 1 << 25)) {
        return DOWN;
    }
    else if (!((LPC_GPIO1->FIOPIN) & 1 << 26)) {
        return LEFT;
    }
		return NODIRECTION;
}

uint16_t readPotentiometer() {
	LPC_PINCON->PINSEL1 &= ~(0x03<<18);
	LPC_PINCON->PINSEL1 |= (0x01<<18);
	LPC_SC->PCONP |= (1 << 12);
	LPC_ADC->ADCR = (1<<2) | (4<<8) | (1<<21);
	LPC_ADC->ADCR |= (1<<24);
	
	return (uint16_t)((LPC_ADC->ADGDR & 0xFFFF) >> 4);
}

void displayText(unsigned char* string, int line) {
  GLCD_DisplayString(line, 1, 1, string);
}

void resetDisplay() {
	GLCD_Init();
	GLCD_Clear(White);
	GLCD_SetBackColor(White);
	GLCD_SetTextColor(Black);
}

void displayVictoryMessage() {
	displayText("You won xd", 1);
}

// moveSnek returns true if the snake has survived the move, false if the snake has died
bool moveSnek(snek_t *snek, dir_t direction) {
	node_t *newHead = malloc(sizeof(node_t));
	
	// Generate the location of the new head depending on the direction passed in.
	if (direction == RIGHT) {
		newHead->x = snek->head->x + 1;
		newHead->y = snek->head->y;
	}
	else if (direction == LEFT) {
		newHead->x = snek->head->x -1;
		newHead->y = snek->head->y;
	}
	else if (direction == UP) {
		newHead->x = snek->head->x;
		newHead->y = snek->head->y - 1; // - 1 bc screen orientation
	}
	else {
		newHead->x = snek->head->x;
		newHead->y = snek->head->y + 1;
	}
	
	/*
		Check if the snake head has collided with a wall (it's impossible for body nodes to collide with a wall)/
		We rely on overflow if the x or y values become negative since they'd overflow to 255 (which is greater than
		32 and 24).
	*/
	if(newHead->x >= 32 || newHead->y >= 24) {
		gameState = DIE;
		return false;
	}
	
	node_t *currNode = snek->head;
	// Check if the snake has collided with itself (i.e. its new head position is identical with one of its nodes).
	for(uint8_t i = 0; i < snek->size - 2; i++) {
		currNode = currNode->next;
		if(newHead -> x == currNode -> x && newHead -> y == currNode -> y) {
			gameState = DIE;
			return false;
		}
	}
	// At this point, currNode is second last node
	// Check for collision with last node (currNode -> next)
	if(newHead->x == currNode->next->x && newHead->y == currNode->next->y) {
		gameState = DIE;
		return false;
	}

	//initialize clear variables to impossible values
	uint16_t xToClear = 0, yToClear = 0;
	
	if(snek->size == 1) {
		xToClear = currNode->x;
		yToClear = currNode->y;
	} else {
		xToClear = currNode->next->x;
		yToClear = currNode->next->y;
		free(currNode->next);
		currNode->next = NULL;
	}
	// Clear the tail square of the snake (because the snake has advanced)
	renderGridSquare(xToClear, yToClear, White);
	
	newHead->next = snek->head;
	snek->head = newHead;
	
	// Check if food has been collected
	if (snek->head->x == snekFood.x && snek->head->y == snekFood.y) {
		snek->size++;
		printf("Size: %d\n", snek->size);
		spawnFood(snek);
	}
	renderGridSquare(snek->head->x, snek->head->y, Black);
	return true;
}

void tReadPotentiometer(void const *arg) {
	while(1) {
		uint16_t potentiometerReading = readPotentiometer();
		printf("%d\n", potentiometerReading);
		if(pot_state == 1 && potentiometerReading > 3500){
			printf("State 1 -> 2");
			pot_state = 2;
		} else if (pot_state == 2 && potentiometerReading < 500) {
			gameOver = true;
			gameState = RESET;
			printf("State 2 -> 1");
			pot_state = 1;
		}
		printf("%d\n", pot_state);
	}
}

// Gameplay Thread
void tGame(void const *arg) {
	snek_t *snek = (snek_t*) arg;
	displayText("Welcome 2 Snek", 1);
	osDelay(1000); 
	// Infinite loop handles repeated plays of the game
	while(1) {
		// Initialize and reset data and screen for the start of the game
		resetDisplay();
		initializeSnek(snek);
		uint8_t prevPotentiometerState = readPotentiometer();
		osDelay(4000);
		// Game loop
		while(!gameOver) {
			osMutexWait(dir_mutex, osWaitForever);
			// Execute move
			gameOver = !moveSnek(snek, dir);			
			osMutexRelease(dir_mutex);
			// Delay decreases (i.e. speed increases) as the snake's size increases.
			uint16_t delay = 1200 - 12 * snek -> size;
			// Caps delay at 300 and ensures overflow (snake size over 100) will result in a delay of 300
			if(delay < 300 || delay > 1200) { 
				delay = 300;
			}
			// Check if the game has been won
			if (snek -> size == WINSCORE ) {
				gameState = WIN;
				gameOver = true;
			}			
			osDelay(delay);
		}
		// Display result of game
		if(gameState == WIN) {
			displayText("You Win!", 1);
		} else if (gameState == DIE) {
			displayText("You Died!", 1);
		} else if ( gameState == RESET) {
			displayText("You are resetting", 1);
		}
		
		// Reset game result for next game
		gameState = 0;
		gameOver = false;
		
		osDelay(5000);	
		
	}
}

// Scoreboard 
void tScoreboard(void const *arg) {
	snek_t *snek = (snek_t*) arg;
	while(1) {
		displayLED(snek->size);
		osDelay(2000 - 12 * snek -> size);
	}
}

osThreadDef(tReadPotentiometer, osPriorityNormal, 1, 0);
osThreadDef(tGame, osPriorityNormal, 1, 0);
osThreadDef(tScoreboard, osPriorityNormal, 1, 0);

/* Main */
int main(void) {
	printf("Initializing\n");
	srand(readPotentiometer());
	osKernelInitialize();
	dir_mutex = osMutexCreate(osMutex(dir_mutex));
	osKernelStart();
	resetDisplay();
	
	snek_t *snek = malloc(sizeof(snek_t));																														
	
	osThreadCreate(osThread(tGame), snek);
	osThreadCreate(osThread(tReadPotentiometer), NULL);
	osThreadCreate(osThread(tScoreboard), snek);
	while(1) {
		// Read joystick state every 100ms(ish) and update the direction variable.  The snake is not
		// allowed to go back on itself (i.e. opposite direction from  the current direction).  A mutex
		// is used to prevent concurrency issues with reads and writes on direction.
		dir_t joystickState = readJoystick();
		if(joystickState != NODIRECTION) {
			osMutexWait(dir_mutex, osWaitForever);
			if((dir == LEFT && joystickState != RIGHT)||(dir == RIGHT && joystickState != LEFT)||
				(dir == UP && joystickState != DOWN) || (dir == DOWN && joystickState != UP)){
					dir = joystickState;
			}
			osMutexRelease(dir_mutex);	
		}
		osDelay(200);
	}
}
