#ifndef MAZE_H
#define MAZE_H

#include <stdint.h>
#include <stdlib.h>

// #include <cmath>
// #include <iostream>

#define maxMapSize 16
#define mapWidth 16
#define mapHeight 16
#define TILE_SIZE 1

int8_t worldMap[mapHeight][mapWidth];

int entryX, entryY, finishX, finishY;
#define STACK_SIZE (mapWidth * mapHeight)


// Stack structure for iterative DFS
typedef struct {
    int x, y;
} Cell;

// Stack variables
Cell stack[STACK_SIZE];
int top = -1;

// Direction vectors for movement (Up, Down, Left, Right)
int dx[] = {0, 0, -1, 1};
int dy[] = {-1, 1, 0, 0};

// Utility function to check if a cell is within the maze boundaries
int isValid(int x, int y) {
    return (x > 0 && x < mapWidth - 1 && y > 0 && y < mapHeight - 1);
}

// Utility function to shuffle directions for random exploration
void shuffleDirections(int directions[]) {
    for (int i = 0; i < 4; i++) {
        int j = random(0, 4) % 4;
        // Swap directions[i] and directions[j]
        int temp = directions[i];
        directions[i] = directions[j];
        directions[j] = temp;
    }
}

// Function to push a cell onto the stack
void push(int x, int y) {
    if (top < STACK_SIZE - 1) {
        stack[++top] = (Cell){x, y};
    }
}

// Function to pop a cell from the stack
Cell pop() {
    return stack[top--];
}

// Function to clear a 3x3 area around the starting position
void clearStartArea(int startX, int startY) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = startX + dx;
            int ny = startY + dy;

            // Ensure the indices are within the maze bounds
            if (nx >= 0 && nx < mapWidth && ny >= 0 && ny < mapHeight) {
                worldMap[ny][nx] = 0; // Clear the tile
            }
        }
    }
}


void clearAreaAroundFinish()
{
   for (int dy = 0; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = finishX + dx;
            int ny = finishY + dy;

            // Ensure the indices are within the maze bounds
            if (nx >= 0 && nx < mapWidth && ny >= 0 && ny < mapHeight) {
                worldMap[ny][nx] = 0; // Clear the tile
            }
        }
    }
}



// Iterative DFS-based maze generation function with 2-tile-wide corridors
void iterativeDFS(int startX, int startY) {
    // Clear a 3x3 area around the start position
    clearStartArea(startX, startY);

    // Initialize stack with the starting cell
    push(startX, startY);
    worldMap[startY][startX] = 0; // Mark the starting cell as a path

    while (top >= 0) {
        Cell current = pop();
        int x = current.x;
        int y = current.y;

        // Randomize directions
        int directions[] = {0, 1, 2, 3}; // Represents Up, Down, Left, Right
        shuffleDirections(directions);

        for (int i = 0; i < 4; i++) {
            int dir = directions[i];
            int nx = x + dx[dir] * 2;
            int ny = y + dy[dir] * 2;

            // Check if the new position is valid and unvisited
            if (isValid(nx, ny) && worldMap[ny][nx] == 1) {
                // Knock down the wall and clear a 2-tile-wide path
                worldMap[y + dy[dir]][x + dx[dir]] = 0; // Clear wall
                worldMap[ny][nx] = 0;                 // Clear target cell

                // Clear adjacent tiles to make the corridor 2 tiles wide
                if (dir == 0 || dir == 1) { // Vertical direction
                    worldMap[y + dy[dir] + 1][x] = 0; // Clear one more row
                } else { // Horizontal direction
                    worldMap[y][x + dx[dir] + 1] = 0; // Clear one more column
                }

                // Push the new cell onto the stack
                push(nx, ny);
            }
        }
    }
}

// Function to initialize the maze with all walls
void initializeMaze() {
    for (int i = 0; i < mapHeight; i++) {
        for (int j = 0; j < mapWidth; j++) {
            worldMap[i][j] = 1; // Set all cells as walls initially
        }
    }
}

// // Function to set entry and finish points inside the maze
// void setEntryAndFinish(int startX, int startY) {
//     // Set the entry point to the starting position
//     entryX = startX;
//     entryY = startY;

//     // Find a valid finish point that is far from the entry point
//     do {
//         finishX = (rand() % (mapWidth - 4)) + 2;  // From 2 to mapWidth-3
//         finishY = (rand() % (mapHeight - 4)) + 2; // From 2 to mapHeight-3
//     } while ((finishX == entryX && finishY == entryY) || worldMap[finishY][finishX] != 0);

//     // Mark the finish point in the maze
//     worldMap[finishY][finishX] = 2;
// }

// Function to set entry and finish points inside the maze
void setEntryAndFinish(int startX, int startY) {
    // Set the entry point to the starting position
    entryX = startX;
    entryY = startY;

    // Find the farthest point from the entry point
    int maxDistance = -1;
    int farthestX, farthestY;

    for (int y = 2; y < mapHeight - 2; y++) {
         for (int x = 2; x < mapWidth - 2; x++) {
            if (worldMap[y][x] == 0) { 
                int distance = abs(x - entryX) + abs(y - entryY); 
                if (distance > maxDistance) { 
                    maxDistance = distance; 
                    farthestX = x; 
                    farthestY = y; 
                } 
            } 
        } 
    }
    // Set the finish point to the farthest point
    finishX = farthestX;
    finishY = farthestY;

    clearAreaAroundFinish(); // Clear a 2x2 area around the finish position


    // Mark the finish point in the maze
    worldMap[finishY][finishX] = 2;
}


// Function to print the maze using ASCII characters
void printMazeSimple() {
    for (int i = 0; i < mapHeight; i++) {
        for (int j = 0; j < mapWidth; j++) {
            if (i == entryY && j == entryX) {
                printf("E "); // Entry point
            } else if (worldMap[i][j] == 1) {
                printf("# "); // Wall
            } else if (worldMap[i][j] == 2) {
                printf("F "); // Finish point
            } else {
                printf("  "); // Path
            }
        }
        printf("\n");
    }
}

// // Function to print the maze using extended ASCII characters
// void printMaze() {
//     for (int i = 0; i < mapHeight; i++) {
//         for (int j = 0; j < mapWidth; j++) {
//             if (i == entryY && j == entryX) {
//                 std::cout << "E "; // Entry point
//         } else if (worldMap[i][j] == 1) {
//             // Check for corners and print accordingly
//                 if (i > 0 && j > 0 && worldMap[i-1][j] == 0 && worldMap[i][j-1] == 0)
//                     std::cout << "┌ "; // Top-left corner
//                 else if (i > 0 && j < mapWidth - 1 && worldMap[i-1][j] == 0 && worldMap[i][j+1] == 0)
//                     std::cout << "┐ "; // Top-right corner
//                 else if (i < mapHeight - 1 && j > 0 && worldMap[i+1][j] == 0 && worldMap[i][j-1] == 0)
//                     std::cout << "└ "; // Bottom-left corner
//                 else if (i < mapHeight - 1 && j < mapWidth - 1 && worldMap[i+1][j] == 0 && worldMap[i][j+1] == 0)
//                     std::cout << "┘ "; // Bottom-right corner
//                 else if (i > 0 && i < mapHeight - 1 && worldMap[i-1][j] == 1 && worldMap[i+1][j] == 1)
//                     std::cout << "│ "; // Vertical wall   
//                 else if (j > 0 && j < mapWidth - 1 && worldMap[i][j-1] == 1 && worldMap[i][j+1] == 1)
//                     std::cout << "─ "; // Horizontal wall
//                 else
//                     std::cout << "# "; // Default wall
//             } else if (worldMap[i][j] == 2) {
//                 std::cout << "F "; // Finish point
//             } else {
//                 std::cout << "  "; // Path
//             }
//         }
//         std::cout << std::endl;
//     }
// }

#endif
