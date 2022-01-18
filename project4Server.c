#include <stdio.h>
#include <stdlib.h>
//rng
#include <time.h>
//networking
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>

#define WIN 1
#define LOOSE -1
#define WORLD_MAX_HEALTH 17

struct ServerSocketCombo {
  int baseSocket;
  int talkSocket;
};

void buildAServer(struct ServerSocketCombo * serversocketcombo, char **argv){
  struct addrinfo hints, *res, *ptr;

  //sets everything in hints to 0, sets some useful default & prevents segmentation faults
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; //IPV 4
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_flags = AI_PASSIVE; // Use my IP address
  //accepts port from command line, IP address will be filled in by function
  getaddrinfo(NULL, argv[1], &hints, &res);

  for (ptr = res;ptr != NULL; ptr=ptr->ai_next) {
    //navigates linked list created by getaddrinfo() looking for correct node
    if ((serversocketcombo->baseSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol))<0){
      //if socket() fails, try it on the next node
      continue;
    }
    if (bind(serversocketcombo->baseSocket, ptr->ai_addr, ptr->ai_addrlen) < 0) {
      //if bind() fails, try it on the next node, also close the socket you just made
      close(serversocketcombo->baseSocket);
      continue;
    }
    //if you get this far, socket() and bind() succeded
    break;
  }

  if (ptr == NULL){
    perror("No suitable addrinfo structs found");
    exit(1);
  }

  //converting IP address in addrinfo to a string to be displayed to user
  char bufferIP[1000];
  inet_ntop(AF_INET, &((struct sockaddr_in *)ptr->ai_addr)->sin_addr, bufferIP, 1000);

  printf("Port: %s\n", argv[1]);
  printf ("IP Address: %s \n", bufferIP);
  puts("Input Port first and IP address second");

  //Socket is made available for connections
  listen(serversocketcombo->baseSocket, 1);

  //socket tries to establish a connection with a client socket
  struct sockaddr_storage clientSockAddr;
  int clientSockAddrSize = sizeof(clientSockAddr);
  serversocketcombo->talkSocket = accept(serversocketcombo->baseSocket, (struct sockaddr *)&clientSockAddr, &clientSockAddrSize);

  //don't need this anymore, let go of my heap
  freeaddrinfo(res);
}

void killAServer(struct ServerSocketCombo * serversocketcombo){
  if (close(serversocketcombo->talkSocket)==-1){
    perror("Talk Socket could not be closed");
  }
  if (close(serversocketcombo->baseSocket)==-1){
    perror("Base Socket could not be closed");
  }
}

void * swapOverSocket (int talkSocket, void * beSwapped, long unsigned int size){
  //acepts a socket fd, void ptr and the #of bytes of memory occupied by whatever the ptr is looking at
  //will send whatever ptr is pointing at over socket, will then recieve data of the same type from the
  //other socket iff they have done the same.
  if ((send(talkSocket,beSwapped, size, 0))==-1){
    perror("Swap Send failed");
    exit(EXIT_FAILURE);
  }

  //printf("size of *beSwapped: %ld\n",sizeof(*beSwapped));

  //allocated memory must be freed outside of function
  void * tempPtr = malloc(size);

  if ((recv(talkSocket, tempPtr, size, 0))==-1) {
     perror("Swap Receive failed:");
     exit(EXIT_FAILURE);
  }

   beSwapped = tempPtr;
   tempPtr = NULL;
   return beSwapped;
}

struct Coordinate{
    //struct that functions will pass between to keep track of user input - is the return type of acceptInput();
    //describes a coordinate
    int num;
    char letter;
};

//10 by 10 grid to be used as game map
struct World{
    int coord[10][10];
    int numHits;
    int moveNum;
};

void coordinateSwap(int talkSocket, struct Coordinate * coord, long unsigned int size){
  //utilzes swapOverSocket() to swap a Coordinate struct between two sockets,
  //sets coordinate argument = to whatever was sent over socket, frees memory allocated in swapOverSocket()
  struct Coordinate *coordPtr;
  //printf("Your coordinate is: %c%d\n", coord->letter, coord->num);
  coordPtr = swapOverSocket(talkSocket, (void*)coord, size);
  coord->letter = coordPtr->letter;
  coord->num = coordPtr->num;
  free(coordPtr);
  coordPtr = NULL;
  //printf("Your coordinate is: %c%d\n", coord->letter, coord->num);
}

void worldSwap(int talkSocket, struct World * world, long unsigned int size){
  //utilzes swapOverSocket() to swap a world struct between two sockets,
  //sets world argument = to whatever was sent over socket, frees memory allocated in swapOverSocket()
  struct World *worldPtr;

  worldPtr = swapOverSocket(talkSocket, (void *)world, size);
  for (int i = 0; i < 10; i++){
    for (int j = 0; j < 10; j++){
      world->coord[i][j] = worldPtr->coord[i][j];
    }
  }
  world->moveNum = worldPtr->moveNum;
  world->numHits = worldPtr->numHits;

  free(worldPtr);
  worldPtr = NULL;
}

//array used to control ship sizes in intitialization
const int SHIP[] = {5,4,3,3,2};

//arrays used to translate the interger values present in the world array into words and make the logfile readable
char *SHIPNAME[] = {"Carrier", "Battleship", "Cruiser", "Submarine", "Destroyer", ""};
char *HITMISS[] = {"Miss", "Hit-"};

//linked list used to contain log information
struct Node{
    int moveNum;
    //1=hit, 0=miss
    int hitMiss;
    int shipname;
    struct Coordinate coordinate;

    struct Node *next;
};

//builds a headPtr
struct Node *initializeList(){
    struct Node *headPtr;
    headPtr = malloc(sizeof(struct Node));
    headPtr->next = NULL;
    return headPtr;
}

//adds new node to list , accepts all information needed for node at the same time
void insertNode (struct Node *headPtr, int moveNum, int hitMiss, int shipname,
                 struct Coordinate coordinate){
    struct Node *newPtr;
    newPtr = malloc(sizeof(struct Node));
    newPtr->next = headPtr->next;
    headPtr->next = newPtr;

    //filling in data
    newPtr->moveNum = moveNum;
    newPtr->hitMiss = hitMiss;
    //shipname corresponds to an array of strings that give names of ships, 5 represents a miss
    newPtr->shipname = shipname;
    if (shipname < 0){
        newPtr->shipname = 5;
    }
    newPtr->coordinate.num = coordinate.num;
    newPtr->coordinate.letter = coordinate.letter;
}

//looks at node and prints information contained to file in readable english
void print_to_file(struct Node *node, FILE *f){
    fprintf(f, "Move #%i, Fired at %c,%i. %s%s\n"
    ,node->moveNum, node->coordinate.letter, node->coordinate.num, HITMISS[node->hitMiss], SHIPNAME[node->shipname]);
}

/* //alternative to printAndFree - keeps nodes in place
void printList(struct Node *headPtr, FILE * f){
    struct Node *tempPtr;
    tempPtr = malloc(sizeof(struct Node));
    tempPtr = headPtr;

    while (tempPtr->next!=NULL){
        // for debugging - printf("moveNum:%i hitMiss:%i shipname:%i,\n",tempPtr->next->moveNum,tempPtr->next->hitMiss, tempPtr->next->shipname);
        print_to_file(tempPtr->next, f);
        tempPtr->next = tempPtr->next->next;
    }
    free(tempPtr);
    tempPtr = NULL;
}
*/

//steps through linked list, prints information found on to file - frees node after reading
void printAndFree(struct Node *headPtr, FILE *f){
    struct Node *tempPtr;
    tempPtr = malloc(sizeof(struct Node));

    tempPtr = headPtr;

    while(headPtr->next!= NULL){
        print_to_file(tempPtr->next, f);

        headPtr = headPtr->next;
        tempPtr->next = NULL;

        free(tempPtr);
        tempPtr = headPtr;
    }
}

//recieves starting coordinates and direction of potential ship placement decides weather ship gets to be placed or not
//return 0 to prevent ship being placed, returns 1 to allow
int check(int *x, int *y, int *direction, int *i, struct World *world){

    //runs an almost identical funtion for each direction
        switch (*direction){
            case 0:
	//loops until j is the size of the ship being placed
            for (int j=0; j < SHIP[*i]; j++){
	//checks that the coordinate is not 0 and that the coordinate is not out of bounds
                if (((*world).coord[*x+j][*y])||((*x+j)>9)){
                    return 0;
                }
            }
            break;

            case 1:
            for (int j=0; j < SHIP[*i]; j++){
                if (((*world).coord[*x-j][*y])||((*x-j)<0)){
                    return 0;
                }
            }
            break;

            case 2:
            for (int j=0; j < SHIP[*i]; j++){
                if (((*world).coord[*x][*y+j])||((*y+j)>9)){
                    return 0;
                }
            }
            break;

            case 3:
            for (int j=0; j < SHIP[*i]; j++){
                if (((*world).coord[*x][*y-j]) ||((*y-j)<0)){
                    return 0;
                }
            }
            break;

            default:
            puts("Something within the RNG has gone wrong");
            break;


        }

        return 1;

}

//dynamically allocates a gameworld and populates it with randomly placed ships
struct World * initialize(struct ServerSocketCombo * serversocketcombo, char **argv)
{
    buildAServer(serversocketcombo, argv);

    struct World *world;
    world = calloc(1,sizeof(struct World));
    if (world == NULL){
        puts("Memory Allocation Failed.");
        exit (0);
    }

    int i = 0, x, y, direction;

    while (i < 5) {
	//x&y and random ints range 0-9, direction is a random int range 0-3
        x = (rand() % 10);
        y = (rand() % 10);
        direction = (rand() % 4);
        //printf("%i %i %i\n", x, y, direction); - for debugging - lets you see the randomly generated ship locations

	//skips rest of loop without iterating if area is invalid
        if (!check(&x, &y, &direction, &i, world)){
            continue;
        }

        //after checking, changes selected values from 0(empty) to 10-14, based on ship(hidden ship)

        switch (direction){
            case 0:
            for (int j=0; j < SHIP[i]; j++){
                (*world).coord[x+j][y] = i+10;
            }
            break;

            case 1:
            for (int j=0; j < SHIP[i]; j++){
                (*world).coord[x-j][y] = i+10;
            }
            break;

            case 2:
            for (int j=0; j < SHIP[i]; j++){
                (*world).coord[x][y+j] = i+10;
            }
            break;

            case 3:
            for (int j=0; j < SHIP[i]; j++){
                (*world).coord[x][y-j] = i+10;
            }
            break;

            default:
            puts("Something within the RNG has gone wrong");
            break;
        }
        i++;
    }
    return world;
}

struct Coordinate acceptInput()
{
    //asks user for a number 0-9, checks input, repeats until satisfactory input is received.
    //repeats the same process for a letter A-J, places info in a Coordinate struct as an int and a char, returns coordinate

    //elements that user will input and will later be placed in Coordinate struct and returned
    int num=0;
    char letter = 'x';

    //goto statement will have program repeatedly ask user to input a number until they do so properly
    accept_num:

    //asks user for info and accepts it with scanf - program fails if user answers with a non-interger
    printf("Please input an integer, 0-9\n");
    scanf(" %i", &num);

    //checks that the number is between 0 and 9 - if it is not, reprimands user and goto's accept_num:
    if ( num > -1 && num < 10 ) {

        //printf("A fine move.\n");

    } else
    {
        printf("That number is out of bounds.\n");
        goto accept_num;
    }

    //goto statement will have program repeatedly ask user to input a letter A-J until they do so properly
    accept_letter:

    //asks user for info and accepts it with scanf - program fails if user answers with something longer than 1 character
    printf("Please input an uppercase letter, A-J\n");
    scanf(" %c", &letter);

    //checks that the letter is between A and J by looking at ascii values
    //- if it is not, reprimands user and goto's accept_letter:
    if (letter > 64 && letter < 75) {

        //printf("A Fine move.\n");
    }
    else{
        printf("That letter is out of bounds.\n");
        goto accept_letter;
    }

    //builds coordinate struct and fills it with user input
    struct Coordinate coordinate;
    coordinate.letter = letter;
    coordinate.num = num;

    //returns coordinate of user input
    return coordinate;
}

//frees memory allocated for world
//prints log info contained in linked lit to file and deallocates linked list
void teardown(struct World *world, struct Node *headPtr, struct ServerSocketCombo * serversocketcombo)
{
    free(world);
    world = NULL;

    FILE *f;
    f = fopen("log.txt", "w");

    printAndFree(headPtr, f);
    fclose(f);
    f = NULL;

    killAServer(serversocketcombo);
}

int updateCoord(int location){
    //0=empty, 10-14=hidden ship, 2=hit ship, 3=miss
    switch (location){
        case 0:
        return 3;
        break;

        case 2:
        puts("A Wasted Turn. You've already hit that ship.");
        return 2;
        break;

        case 3:
        puts("A Wasted Turn. You know that space is empty. This is a game. The ships don't move.");
        return 3;
        break;

        default:
        return 2;
        break;

    }
}

//accepts the gameworld and the most recent user input as arguments - uses input to change gameworld
void updateWorld(struct World *world, struct Coordinate *coordinate, struct Node *headPtr){

    (*world).moveNum++;
    printf("\nMove #%i:\n",(*world).moveNum);

    if ((*world).coord[(*coordinate).letter-65][(*coordinate).num] > 9){
            puts("Hit!");
            (*world).numHits++;
            insertNode(headPtr, (*world).moveNum, 1,
                       (*world).coord[(*coordinate).letter-65][(*coordinate).num]-10,*coordinate);

        } else {
            insertNode(headPtr, (*world).moveNum, 0,
                       (*world).coord[(*coordinate).letter-65][(*coordinate).num]-10,*coordinate);
        }

    switch ((*coordinate).letter)
    {
        case 'A':
        //follows user-input-coordinate to spot, updates that spot
        (*world).coord[0][(*coordinate).num] = updateCoord((*world).coord[0][(*coordinate).num]);
        break;
        case 'B':
        (*world).coord[1][(*coordinate).num] = updateCoord((*world).coord[1][(*coordinate).num]);
        break;
        case 'C':
        (*world).coord[2][(*coordinate).num] = updateCoord((*world).coord[2][(*coordinate).num]);
        break;
        case 'D':
        (*world).coord[3][(*coordinate).num] = updateCoord((*world).coord[3][(*coordinate).num]);
        break;
        case 'E':
        (*world).coord[4][(*coordinate).num] = updateCoord((*world).coord[4][(*coordinate).num]);
        break;
        case 'F':
        (*world).coord[5][(*coordinate).num] = updateCoord((*world).coord[5][(*coordinate).num]);
        break;
        case 'G':
        (*world).coord[6][(*coordinate).num] = updateCoord((*world).coord[6][(*coordinate).num]);
        break;
        case 'H':
        (*world).coord[7][(*coordinate).num] = updateCoord((*world).coord[7][(*coordinate).num]);
        break;
        case 'I':
        (*world).coord[8][(*coordinate).num] = updateCoord((*world).coord[8][(*coordinate).num]);
        break;
        case 'J':
        (*world).coord[9][(*coordinate).num] = updateCoord((*world).coord[9][(*coordinate).num]);
        break;
        default:
        puts("An error has occured in updateWorld");
    }

}

//masks grid locations with information the player should not be aware of with blank space
//-translates numbers into more player friendly chars
char playerView(int location){
    switch (location){
        case 0:
        return ' ';
        break;
        case 1:
        return '!';
        break;
        case 2:
        return 'X';
        break;
        case 3:
        return 'O';
        break;
        default:
        return ' ';
        break;
    }
}

void displayWorld(struct World *world){
    //builds an ASCII grid populated primarily by spaces,
    //fills up with more information as game progresses
    printf("X = hit, O = miss\n |A|B|C|D|E|F|G|H|I|J|\n");

    //uses the standard system of nested loops to step through a multi-dimensional array
    for(int j = 0; j < 10; j++)
    {
        printf("%i|", j);

        for(int i = 0; i<10; i++){
            //printf("%i|",(*world).coord[i][j]);
            printf("%c|",playerView((*world).coord[i][j]));
        }
        //indents
        puts("");
    }
}


int main(int argc, char *argv[]) {
    if (argc == 1){
      puts("Too few arguments; input a Port");
      return -1;
    }

    //uses currect time as seed of rng used in initialize
    srand(time(0));

    //builds linked list to be used to store log info
    struct Node * headPtr;
    headPtr = initializeList();

    //world is a 10-10 array of ints used as a gameboard and a life counter to check if the game is over
    struct World *world;
    //Coordinate is an int and a char used to temporarily store player input
    struct Coordinate coordinate;

    struct ServerSocketCombo serversocketcombo = {0,0};

    //allocates memory for world, generates random map of ships
    world = initialize(&serversocketcombo, argv);

    //loops until every ship has been hit
    int flag = 0;
    while (1){
        worldSwap(serversocketcombo.talkSocket, world, sizeof(*world));

        //creates coordinate struct and prompts user to fill it
        coordinate = acceptInput();
        puts("Waiting for opponent to move...");
        coordinateSwap(serversocketcombo.talkSocket, &coordinate, sizeof(coordinate));

        //takes user input and uses it to update gamestate and logfile
        updateWorld(world, &coordinate, headPtr);

        if((*world).numHits == WORLD_MAX_HEALTH){
          flag = LOOSE;
        }

        worldSwap(serversocketcombo.talkSocket, world, sizeof(*world));

        if((*world).numHits == WORLD_MAX_HEALTH){
          flag = WIN;
        }
        //displays gameboard to user
        displayWorld(world);

        if (flag){
          if (flag == WIN){
            puts("You win!");
            break;
          } else if (flag == LOOSE){
            puts("You Loose!");
            break;
          } else{
            puts("An issue occured in the flags");
            break;
          }
        }
    }

    //frees allocated memory & socket
    teardown(world, headPtr, &serversocketcombo);

    return 0;
}
