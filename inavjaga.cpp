#include "include/cross_platform.hpp"
#include "inavjaga.hpp"
#include "src/serialize.hpp"
#include <sstream>
#include <algorithm>
#include <memory>
#include <future>
#include <thread>
#include <chrono>
#include <mutex>
#include <stack>
#include <iostream>
#include <random>
#include <queue>
#include <map>

std::shared_ptr<Player> Player::localPlayer;
std::vector<std::shared_ptr<Player>> Player::players;
std::vector<std::shared_ptr<Wall>> Wall::walls;
std::vector<std::shared_ptr<Bullet>> Bullet::bullets;
std::vector<std::shared_ptr<EnemyBullet>> EnemyBullet::enemyBullets;
std::vector<std::shared_ptr<Chest>> Chest::chests;
std::vector<std::shared_ptr<Portal>> Portal::portals;
std::vector<std::shared_ptr<Mine>> Mine::mines;
std::vector<std::shared_ptr<Archer>> Archer::archers;
std::vector<std::shared_ptr<WormBody>> WormBody::wormBodies;
std::vector<std::shared_ptr<Worm>> Worm::worms;

std::bernoulli_distribution dumbMoveDistribution(DUMB_MOVE_PROBABILITY);

std::shared_ptr<sista::SwappableField> field;
sista::Cursor cursor;
sista::Border border(
    '@', {
        RGB_ROCKS_FOREGROUND,
        RGB_ROCKS_BACKGROUND,
        sista::Attribute::BRIGHT
    }
);
std::queue<MoveEvent> movesBuffer = std::queue<MoveEvent>();
std::string gameState = std::string();
const size_t pastGameStatesBufferSize = 20; // Supporting up to 2s of latency
std::string pastGameStates[pastGameStatesBufferSize] = {std::string()};
std::mutex movesBufferMutex = std::mutex();
std::mutex streamMutex = std::mutex();
std::mutex stderrMutex = std::mutex();
std::mutex gameStateMutex = std::mutex();
bool speedup = false;
int lastDeathFrame = 0;
bool end = false;


int main(int argc, char* argv[]) {
    #ifdef __APPLE__
        term_echooff();
    #endif
    /// @warning I changed this one
    std::ios_base::sync_with_stdio(true);
    sista::resetAnsi(); // Reset the settings

    if (argc < 3) {
        std::cerr << "The correct format is: ./inavjaga[Server] <ip-address> <moves-tcp-port> <sync-tcp-port>" << std::endl;
        return 1;
    }

    #if SERVER
    field = std::make_shared<sista::SwappableField>(WIDTH, HEIGHT);
    sista::Coordinates spawn = sista::Coordinates(SPAWN_COORDINATES_Y, SPAWN_COORDINATES_X);
    Player::localPlayer = std::make_shared<Player>(spawn);
    Player::localPlayer->respawnCoordinates = {RESPAWN_COORDINATES_Y, RESPAWN_COORDINATES_X};
    Player::localPlayer->setSettings(Player::localPlayerStyle);
    Player::localPlayer->mode = Player::Mode::BULLET;
    Player::players.push_back(Player::localPlayer);
    field->addPawn(Player::localPlayer);
    #if INTRO
    intro();
    #endif
    #if TUTORIAL
    tutorial();
    #endif
    #endif // The client instead will get its ID from the server

    #if CLIENT
    std::shared_ptr<ClientInavjagaGSPIO> connectionToServer = connectClientToServer(
        socket(AF_INET, SOCK_STREAM, IPPROTO_TCP),
        socket(AF_INET, SOCK_STREAM, IPPROTO_TCP),
        argv[1], argv[2], argv[3]
    );
    #elif SERVER
    int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int serverSyncSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    bindServerSocketToPort(serverSocket, argv[1], argv[2]);
    bindServerSocketToPort(serverSyncSocket, argv[1], argv[3]);
    std::vector<std::shared_ptr<ServerInavjagaGSPIO>> clientConnections = waitForConnections(serverSocket, serverSyncSocket);
    #endif
    // This is just the stage in which we have established connections, but the handshake still misses

    #if SERVER
    uint32_t seed = randomDevice();
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "The seed is " << seed << std::endl;
    }
    #endif
    for (std::shared_ptr<ServerInavjagaGSPIO> clientConnection : clientConnections) {
        if (clientConnection == nullptr) continue;
        clientConnection->sendRandomSeed(seed);
    }
    for (std::shared_ptr<ServerInavjagaGSPIO> clientConnection : clientConnections) {
        if (clientConnection == nullptr) continue;
        if (!clientConnection->waitYes(2000)) {
            {
                std::unique_lock lock(stderrMutex);
                std::cerr << "No acknowledgment received from client " << clientConnection.get();
            }
        }
    }
    #elif CLIENT
    uint32_t seed;
    while (true) {
        try {
            seed = connectionToServer->recvRandomSeed(1000);
            break;
        } catch (std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
    }
    connectionToServer->sendYes();
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Random seed is " << seed << std::endl;
    }
    #endif
    #endif
    rng.seed(seed);

    #if SERVER
    for (std::shared_ptr<ServerInavjagaGSPIO> clientConnection : clientConnections) {
        if (clientConnection == nullptr) continue;
        if (clientConnection->sendConstants()) {
            sista::Coordinates spawn_client = negotiateCoordinates(field, clientConnection);
            Player::players.push_back(std::make_shared<Player>(spawn_client));
            Player::players.back()->id = Player::players.size() - 1;
            Player::players.back()->respawnCoordinates = spawn_client;
            field->addPawn(Player::players.back());
        }
    }
    for (size_t i = 0; i < clientConnections.size(); i++) {
        if (clientConnections[i] == nullptr) continue;
        clientConnections[i]->sendPlayers(Player::players, i);
    }
    std::vector<player_id_t> nonReady;
    for (size_t i = 0; i < clientConnections.size(); i++) {
        #if DEBUG
        {
            std::unique_lock lock(stderrMutex);
            std::cerr << "Connection number " << i << " is " << clientConnections[i].get() << std::endl;
        }
        #endif
        if (clientConnections[i] == nullptr) continue;
        #if DEBUG
        {
            std::unique_lock lock(stderrMutex);
            std::cerr << "Player number " << i << " is " << Player::players[i]->getCoordinates().x << std::endl;
        }
        #endif
        if (!clientConnections[i]->recvReady()) {
            Player::players[i]->connected = false;
            clientConnections[i] = nullptr;
            nonReady.push_back(i);
        }
    }
    for (size_t i = 0; i < nonReady.size(); i++) {
        field->removePawn(Player::players[nonReady[i]].get());
    }
    for (size_t i = 1; i < Player::players.size(); i++) {
        if (!Player::players[i]->connected) {
            for (size_t j = 0; j < clientConnections.size(); j++) {
                if (clientConnections[j] == nullptr) continue;
                clientConnections[j]->sendMove(MoveEvent{(player_id_t)i, 'Q'});
            }
            break;
        }
    }
    #elif CLIENT
    std::map<std::string, std::variant<int, float>> constants = connectionToServer->recvConstants();
    connectionToServer->sendYes();
    setConstantsToReceivedValues(constants);
    field = std::make_shared<sista::SwappableField>(WIDTH, HEIGHT);
    placeClientPlayer(connectionToServer);
    // This will also place the Player::localPlayer in the right position and assign the Id to it
    Player::players = connectionToServer->recvPlayers();
    for (size_t i = 0; i < Player::players.size(); i++) {
        if (Player::players[i] == nullptr) continue;
        #if DEBUG
        {
            std::unique_lock lock(stderrMutex);
            std::cerr << "Processing player " << i << " with identifier " << Player::players[i]->id << std::endl;
        }
        #endif
        if (i != Player::localPlayerId) {
            #if DEBUG
            {
                std::unique_lock lock(stderrMutex);
                std::cerr << "Consider that the local player ID is " << Player::localPlayerId << std::endl;
                std::cerr << "Added to players, will try to insert at {y," << Player::players[i]->getCoordinates().x << std::endl;
            }
            #endif
            field->addPawn(Player::players[i]);
        }
    }
    connectionToServer->sendReady();
    #endif

    generateTunnels();
    spawnInitialEnemies();
    sista::clearScreen(true);
    field->print(border);

    LocalInavjagaIO* localIO;
    RemoteInavjagaIO* remoteIO;
    #if CLIENT
    localIO = new ClientLocalInavjagaIO(connectionToServer);
    remoteIO = new ClientRemoteInavjagaIO(connectionToServer);
    #elif SERVER
    localIO = new ServerLocalInavjagaIO(clientConnections);
    remoteIO = new ServerRemoteInavjagaIO(clientConnections);
    #endif
    std::thread localInputThread(input<LocalInavjagaIO>, localIO);
    std::thread remoteInputThread(input<RemoteInavjagaIO>, remoteIO);
    #if CLIENT
    std::thread remoteGameStateThread(recvUpdates, remoteIO);
    #elif SERVER
    std::thread remoteGameStateThread(updateClients, remoteIO);
    #endif

    auto start = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> delta;
    for (int i=0; !end; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(
            ((int)(FRAME_DURATION / (std::pow(1 + (int)speedup, 2))))
            - std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()
        )); // If there is speedup, the waiting time is reduced by a factor of 4
        #if DEBUG
        {
            delta = std::chrono::high_resolution_clock::now() - start;
            std::unique_lock stderrLock(stderrMutex);
            std::cerr << "Frame number " << i << " took " 
                      << std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()
                      << "ms" << std::endl;
        }
        #endif
        start = std::chrono::high_resolution_clock::now();

        fullProcessFrame(i);
        std::flush(std::cout);

        {
            std::unique_lock lockGameState(gameStateMutex);
            gameState = std::to_string(i) + "," + serialize(rng) + "," + serializeGameState();
            #if CLIENT
            pastGameStates[i % pastGameStatesBufferSize] = gameState;
            #endif
        }

        delta = std::chrono::high_resolution_clock::now() - start;
        #if DEBUG
        {
            std::unique_lock stderrLock(stderrMutex);
            std::cerr << "\tFrame number " << i << " took " 
                      << std::chrono::duration_cast<std::chrono::microseconds>(delta).count()
                      << "µs" << std::endl;
        }
        #endif
    }

    end = true; // Needed to ensure the input function returns and the thread localInputThread gets joined
    deallocateAll();
    localInputThread.join();
    remoteInputThread.join();
    remoteGameStateThread.join();
    field->clear();
    cursor.goTo(72, 0); // Move the cursor to the bottom of the screen, so the terminal is not left in a weird state
    std::this_thread::sleep_for(std::chrono::seconds(1)); // Give the time to see the final screen
    flushInput();
    getch();
    #ifdef __APPLE__
        tcsetattr(0, TCSANOW, &orig_termios);
    #endif
}

bool endConditions() {
    // Check for enemies in the top right corner area
    for (unsigned short row = 0; row < TUNNEL_UNIT * 2; row++) {
        for (unsigned short column = WIDTH - 1; column >= WIDTH - TUNNEL_UNIT * 2; column--) {
            if (field->isOccupied(row, column)) {
                if (((Entity*)field->getPawn(row, column))->type == Type::ARCHER
                    || ((Entity*)field->getPawn(row, column))->type == Type::WORM_HEAD) {
                    printEndInformation(EndReason::TOUCHDOWN);
                    return true;
                }
            }
        }
    }
    return false;
}

void processMoves() {
    std::unique_lock lock(movesBufferMutex);
    while (!movesBuffer.empty()) {
        MoveEvent current = movesBuffer.front();
        movesBuffer.pop();
        act(current);
    }
}

void processFrame() {
    for (int k = 0; k < BULLET_SPEED; k++) {
        // move player bullets
        for (unsigned j = 0; j < Bullet::bullets.size(); j++) {
            Bullet* bullet = Bullet::bullets[j].get();
            if (bullet == nullptr) continue;
            if (bullet->collided) continue;
            bullet->move();
        }
        // collect collided player bullets, then remove them (safe even if remove() mutates Bullet::bullets)
        {
            std::vector<std::shared_ptr<Bullet>> to_remove;
            for (auto &bp : Bullet::bullets) {
                Bullet* bullet = bp.get();
                if (bullet && bullet->collided) to_remove.push_back(bp);
            }
            for (auto &bp : to_remove) bp->remove();
        }

        // move enemy bullets
        for (unsigned j = 0; j < EnemyBullet::enemyBullets.size(); j++) {
            EnemyBullet* bullet = EnemyBullet::enemyBullets[j].get();
            if (bullet == nullptr) continue;
            if (bullet->collided) continue;
            bullet->move();
        }
        // collect collided enemy bullets, then remove them
        {
            std::vector<std::shared_ptr<EnemyBullet>> to_remove;
            for (auto &bp : EnemyBullet::enemyBullets) {
                EnemyBullet* bullet = bp.get();
                if (bullet && bullet->collided) to_remove.push_back(bp);
            }
            for (auto &bp : to_remove) bp->remove();
        }
    }
    for (unsigned j = 0; j < Mine::mines.size(); j++) {
        if (j >= Mine::mines.size()) break;
        Mine* mine = Mine::mines[j].get();
        if (mine->triggered) {
            if (Mine::explosion(rng)) {
                mine->explode();
            }
        }
    }
    for (auto archer : Archer::archers) {
        if (Archer::moving(rng)) {
            archer->move();
        }
        if (Archer::shooting(rng)) {
            archer->shoot();
        }
    }
    for (unsigned j = 0; j < Worm::worms.size(); j++) {
        Worm* worm = Worm::worms[j].get();
        if (worm == nullptr) continue;
        if (worm->collided) continue;
        if (Worm::turning(rng)) {
            worm->turn();
        }
        if (Worm::moving(rng)) {
            worm->move();
        }
    }
    for (auto &wp : Worm::worms) {
        Worm* worm = wp.get();
        if (worm && worm->collided) {
            worm->die();
        }
    }
    for (unsigned j = 0; j < Mine::mines.size(); j++) {
        if (j >= Mine::mines.size()) break;
        Mine* mine = Mine::mines[j].get();
        if (!mine->triggered) {
            for (int k=-MINE_SENSITIVITY_RADIUS; k<=MINE_SENSITIVITY_RADIUS; k++) {
                for (int h=-MINE_SENSITIVITY_RADIUS; h<=MINE_SENSITIVITY_RADIUS; h++) {
                    if (k == 0 && h == 0) continue;
                    sista::Coordinates target = mine->getCoordinates() + sista::Coordinates(k, h);
                    if (field->isOutOfBounds(target)) {
                        continue;
                    } else if (field->isOccupied(target)) {
                        Entity* entity = (Entity*)field->getPawn(target);
                        if (entity->type == Type::WORM_HEAD) {
                            mine->trigger();
                        }
                    }
                }
            }
        }
    }
    if (!Wall::walls.empty() && Wall::wearing(rng)) {
        for (int j = 0; j < DAMAGED_WALLS_COUNT; j++) {
            if (Wall::walls.empty()) break;
            int index = std::uniform_int_distribution<int>(0, Wall::walls.size() - 1)(rng);
            Wall::walls[index]->takeHit();
        }
    }
}

bool revivePlayers() {
    bool localPlayerDied = false;
    for (size_t p = 0; p < Player::players.size(); p++) {
        if (Player::players[p] == nullptr) continue;
        if (Player::players[p]->dead) {
            processDeath(Player::players[p]);
            Player::players[p]->dead = false;
            if (p == Player::localPlayerId) {
                localPlayerDied = true;
            }
        }
    }
    return localPlayerDied;
}

void processDeath(std::shared_ptr<Player> player) {
    std::lock_guard<std::mutex> lock(streamMutex);
    sista::Coordinates deathCoordinates = player->getCoordinates();
    field->movePawn(player.get(), player->respawnCoordinates);
    if (DROP_INVENTORY_ON_DEATH) {
        std::shared_ptr<Chest> c = std::make_shared<Chest>(
            deathCoordinates, Inventory{
                player->inventory.clay,
                player->inventory.bullets,
                0
            }
        );
        Chest::chests.push_back(c);
        field->addPrintPawn(c);
    }
    player->inventory.clay = 0;
    player->inventory.bullets = 0;
}

void fullProcessFrame(int i) {
    if (revivePlayers()) {
        lastDeathFrame = i;
    }
    if (lastDeathFrame && i - lastDeathFrame == 20) {
        // After 20 frames it deletes the death reason
        std::lock_guard<std::mutex> lock(streamMutex); // Lock stays until scope ends
        reprint();
    }

    processMoves(); // This has to be done earlier than the lock as act() wants it
    std::lock_guard<std::mutex> lock(streamMutex); // Lock stays until scope ends
    processFrame();
    if (i % MEAT_DURATION_PERIOD == MEAT_DURATION_PERIOD - 1) {
        Player::localPlayer->inventory.meat--;
    }
    spawnEnemies();
    printSideInstructions(i);
    // Check for negative amount of meat
    if (Player::localPlayer->inventory.meat < 0) {
        printEndInformation(EndReason::STARVED);
        Player::localPlayer->dead = true;
        end = true;
    }
    end = endConditions();
}

void updateClients(RemoteInavjagaIO* remote_) {
    ServerRemoteInavjagaIO* remote = (ServerRemoteInavjagaIO*)remote_;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    while (!end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        std::unique_lock lock(gameStateMutex);
        if (gameState.empty()) continue; // We haven't gone over a frame yet
        remote->sendGameStateToAll(gameState);
    }
}

void recvUpdates(RemoteInavjagaIO* remote_) {
    ClientRemoteInavjagaIO* remote = (ClientRemoteInavjagaIO*)remote_;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    while (!end) {
        std::unique_lock lock(gameStateMutex);
        std::string serverGameState;
        if ((serverGameState = remote->recvGameState()) == gameState) continue;
        #if DEBUG
        {
            std::unique_lock lock(stderrMutex);
            std::cerr << "The game state is: " << serverGameState << std::endl;
        }
        #endif
        if (serverGameState.empty()) continue;
        #if DEBUG
        {
            std::unique_lock lock(stderrMutex);
            std::cerr << "The game state did not match with the server, latency might be there" << std::endl;
        }
        #endif
        // Parsing the frame number from the server
        std::istringstream isServer(serverGameState);
        std::string frameString;
        std::getline(isServer, frameString, ',');
        if (std::empty(frameString) || !std::isalnum(frameString[0])) {
            #if DEBUG
            {
                std::unique_lock lock(stderrMutex);
                std::cerr << "We received absolute emptiness from the server on the synchronization channel" << std::endl;
            }
            #endif
            return;
        }
        int serverFrame = stoi(frameString);
        // Parsing the frame number from the client
        std::istringstream isClient(gameState);
        std::getline(isClient, frameString, ',');
        if (std::empty(frameString) || !std::isalnum(frameString[0])) {
            #if DEBUG
            {
                std::unique_lock lock(stderrMutex);
                std::cerr << "The client has an absolutely empty or malformed game state" << std::endl;
            }
            #endif
            return;
        }
        int clientFrame = stoi(frameString);
        if (clientFrame == serverFrame) {
            // This means that there is a mismatch and there will be some work to do
            std::unique_lock lock(streamMutex);
            restoreGameState(serverGameState);
        } else {
            if (pastGameStates[serverFrame % pastGameStatesBufferSize] == serverGameState) {
                // This means we are just some frames ahead, we can keep on going
                #if DEBUG
                {
                    std::unique_lock lock(stderrMutex);
                    std::cerr << "Because of latency, we are " << clientFrame - serverFrame << " frames ahead of the server" << std::endl;
                }
                #endif
                continue;
            } else {
                // This means that we lost synchronization some frames ago
                /// @note we assume that the server cannot be ahead of the client,
                /// since it would require a clock unsync greater than the latency
                /// @note we assume that latency cannot be greater than 
                /// pastGameStatesBufferSize * FRAME_DURATION milliseconds
                {
                    std::unique_lock lock(streamMutex);
                    restoreGameState(serverGameState);
                }
                for (int i = serverFrame + 1; i <= clientFrame; i++) {
                    fullProcessFrame(i);
                }
            }
        }
    }
}

/** @brief Restores the field and the entities from a string
 * @param serverGameState A string in the format defined by serializeGameState
 * @cite serialize.cpp
 * @note Refer to serializeGameState for reverse implementation details
 * @warning We assume the gameStateMutex to be already locked before the function is called
 * @todo The whole function is still empty
 */
void restoreGameState(const std::string& serverGameState) {
    std::istringstream state(serverGameState);
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Restoring the game state to \n\t" << serverGameState << std::endl;
    }
    #endif
    {
        std::unique_lock lock(streamMutex);
        deallocateAll();
        field->clear();
    }

    std::string frameString; // We are lk trashing this anyway
    std::getline(state, frameString, ',');

    state >> rng; // We restore the rng state
    char _;
    state >> _ >> _; // Comma and classTermination
    std::string entities;
    std::getline(state, entities, classTermination[0]);
    deserializeEntities<Archer>(entities);
    std::getline(state, entities, classTermination[0]);
    deserializeEntities<Bullet>(entities);
    std::getline(state, entities, classTermination[0]);
    deserializeEntities<Chest>(entities);
    std::getline(state, entities, classTermination[0]);
    deserializeEntities<EnemyBullet>(entities);
    std::getline(state, entities, classTermination[0]);
    deserializeEntities<Mine>(entities);
    std::getline(state, entities, classTermination[0]);
    deserializeEntities<Player>(entities);
    std::getline(state, entities, classTermination[0]);
    deserializeEntities<Portal>(entities);
    std::getline(state, entities, classTermination[0]);
    deserializeEntities<Wall>(entities);
    std::getline(state, entities, classTermination[0]);
    deserializeEntities<Worm>(entities);
    /// @todo finish this function
}

template <class T>
void deserializeEntities(const std::string& entities) {
    std::istringstream entitiesStream(entities);
    std::string entity;
    for (int counter = 0; std::getline(entitiesStream, entity, ';'); counter++) {
        std::shared_ptr<T> entityObject = deserialize<T>(entity);
        T::entities->push_back(entityObject);
        field->addPawn(entityObject);
        if (std::is_same<T, Portal>::value) {
            if (counter % 2 == 1) {
                (*Portal::entities)[counter - 1]->exit = (*Portal::entities)[counter];
                (*Portal::entities)[counter]->exit = (*Portal::entities)[counter - 1];
            }
        }
    }
}

void intro() {
    #if defined(__linux__)
    std::future<int> future = std::async([](){ return static_cast<int>(getch()); });
    std::chrono::duration refresh = std::chrono::milliseconds(FRAME_DURATION);
    #elif defined(_WIN32)
    std::future<int> future = std::async(getch);
    // Lower refresh rate for Windows to contain the flickering effect in the terminal
    std::chrono::duration refresh = std::chrono::milliseconds(FRAME_DURATION * 10);
    #elif defined(__APPLE__)
    std::future<int> future = std::async(getchar);
    std::chrono::duration refresh = std::chrono::milliseconds(FRAME_DURATION);
    #endif
    while (true) {
        std::cout << "Make sure that the following hash signs fit the best in a line in your terminal.\n";
        std::cout << "Use ctrl+<minus> and ctrl+<plus> or ctrl+<mouse-scroll> to resize your terminal.\n";
        std::cout << "Maximize your terminal window for an optimal view on the field, then enter any key to proceed.\n";
        field->print(border);
        sista::resetAnsi();
        cursor.goTo(8, (unsigned short)(WIDTH / 2.1));
        std::cout << "Inävjaga";
        cursor.goTo(TUNNEL_UNIT * 3 + 7, TUNNEL_UNIT * 2 + 2);
        Player::playerStyle.apply();
        std::cout << "Inävjaga v" << VERSION;
        cursor.goTo(TUNNEL_UNIT * 3 + 7, (unsigned short)(WIDTH / 2.6));
        sista::resetAnsi();
        sista::setAttribute(sista::Attribute::ITALIC);
        sista::setAttribute(sista::Attribute::FAINT);
        std::cout << " originally by ";
        sista::resetAnsi();
        Player::playerStyle.apply();
        std::cout << AUTHOR << "     " << DATE;
        sista::resetAnsi();
        cursor.goTo(TUNNEL_UNIT * 3 + 9, (unsigned short)(WIDTH / 3.5));
        sista::setAttribute(sista::Attribute::UNDERSCORE);
        std::cout << "https://github.com/FLAK-ZOSO/Inavjaga";
        std::cout << std::flush;

        if (future.wait_for(refresh) == std::future_status::ready) {
            sista::clearScreen(true);
            return;
        }
        sista::clearScreen(true);
    }
}

void tutorial() {
    field->print(border);
    printSideInstructions(0);

    cursor.goTo(TUNNEL_UNIT * 3 + 3 + 1, 4);
    sista::resetAnsi();
    std::cout << "When you want to skip the tutorial, click 'n' at any point";
    cursor.goTo(TUNNEL_UNIT * 3 + 3 + 2, 4);
    std::cout << "To disable it, set TUTORIAL to 0 in constants.hpp and recompile";

    cursor.goTo(TUNNEL_UNIT * 4 + 4, 4);
    sista::resetAnsi();
    std::cout << "You are the ";
    Player::playerStyle.apply();
    std::cout << "$ player";
    sista::resetAnsi();
    std::cout << ", try moving around a bit" << std::endl;

    char input_;
    flushInput();
    for (int i = 0; i < 5; i++) {
        input_ = getch();
        if (movementKeys.find(input_) != movementKeys.end()) {
            act(input_);
        } else if (input_ == 'n') {
            flushInput();
            sista::clearScreen(true);
            return;
        }
        std::cout << std::flush;
    }
    {
        auto a = std::make_shared<Archer>(sista::Coordinates{Player::localPlayer->getCoordinates().y, (unsigned short)(WIDTH - 3 * TUNNEL_UNIT - 1)});
        Archer::archers.push_back(a);
        field->addPrintPawn(a);
    }

    cursor.goTo(TUNNEL_UNIT * 5 + 3 + 1, 4);
    sista::resetAnsi();
    std::cout << "An ";
    Archer::archerStyle.apply();
    std::cout << "Archer";
    sista::resetAnsi();
    std::cout << "! Enter bullet mode and shoot it!" << std::endl;

    flushInput();
    input_ = ' ';
    while (input_ != 'j' && input_ != 'J') {
        input_ = getch();
        if (movementKeys.find(input_) != movementKeys.end())
            continue;
        if (input_ == 'n') {
            flushInput();
            sista::clearScreen(true);
            return;
        } else if (input_ == 'j' || input_ != 'J') {
            act(input_);
        }
        std::cout << std::flush;
    }

    while (!Archer::archers.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_DURATION));
        Bullet::bullets[0]->move();
        std::cout << std::flush;
    }

    cursor.goTo(TUNNEL_UNIT * 6 + 3 + 1, 4);
    sista::resetAnsi();
    std::cout << "You can loot its ";
    Chest::chestStyle.apply();
    std::cout << "Chest";
    sista::resetAnsi();
    std::cout << ". Enter collect mode and pick it up." << std::endl;

    while (!Chest::chests.empty()) {
        input_ = getch();
        if (input_ == 'n') {
            flushInput();
            sista::clearScreen(true);
            return;
        }
        act(input_);
        printSideInstructions(0);
        std::cout << std::flush;
    }

    cursor.goTo(TUNNEL_UNIT * 7 + 3, 4);
    sista::resetAnsi();
    std::cout << "Each wall layer can be traversed by you through ";
    Portal::portalStyle.apply();
    std::cout << "& Portals";
    sista::resetAnsi();
    std::cout << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    cursor.goTo(TUNNEL_UNIT * 7 + 3 + 1, 4);
    std::cout << "You might also encounter ";
    WormBody::wormBodyStyle.apply();
    std::cout << ">>>>>>>H Snakes";
    sista::resetAnsi();
    std::cout << ", I hope you have good aim.";
    std::cout << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    cursor.goTo(TUNNEL_UNIT * 8 + 4, TUNNEL_UNIT * 2 + 3);
    sista::resetAnsi();
    std::cout << "Protect the red area. And don't starve: you consume meat.";
    
    sista::ANSISettings highlight(
        sista::ForegroundColor::RED,
        sista::BackgroundColor::RED,
        sista::Attribute::BLINK
    );
    std::vector<std::shared_ptr<sista::Pawn>> highlightPawns;
    for (int i = 0; i < TUNNEL_UNIT * 2; i++) {
        for (int j = WIDTH - TUNNEL_UNIT * 2; j < WIDTH; j++) {
            auto pawn = std::make_shared<sista::Pawn>(' ', sista::Coordinates{(unsigned short)i, (unsigned short)j}, highlight);
            highlightPawns.push_back(pawn);
            field->addPrintPawn(pawn);
        }
    }
    std::cout << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    cursor.goTo(TUNNEL_UNIT * 8 + 3 + 4, WIDTH / 3);
    sista::resetAnsi();
    sista::setAttribute(sista::Attribute::ITALIC);
    sista::setAttribute(sista::Attribute::BLINK);
    std::cout << "Press any key to play Inävjaga...";
    std::cout << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    flushInput();
    input_ = getch();

    while (!highlightPawns.empty()) {
        field->erasePawn(highlightPawns.back().get());
        highlightPawns.pop_back();
    }

    flushInput();
    sista::clearScreen(true);
}

void printSideInstructions(int i) {
    // Print the inventory
    sista::resetAnsi();
    cursor.goTo(3, WIDTH + 10);
    sista::setAttribute(sista::Attribute::BRIGHT);
    std::cout << "Inventory\n";
    sista::resetAttribute(sista::Attribute::BRIGHT);
    cursor.goTo(4, WIDTH + 10);
    std::cout << "Clay: " << Player::localPlayer->inventory.clay << "   \n";
    cursor.goTo(5, WIDTH + 10);
    std::cout << "Bullets: " << Player::localPlayer->inventory.bullets << "   \n";
    cursor.goTo(6, WIDTH + 10);
    std::cout << "Meat: " << Player::localPlayer->inventory.meat << "   \n";
    cursor.goTo(7, WIDTH + 10);
    std::cout << "Mode: ";
    switch (Player::localPlayer->mode) {
        case Player::Mode::COLLECT:
            std::cout << "Collect";
            break;
        case Player::Mode::BULLET:
            std::cout << "Bullet";
            break;
        case Player::Mode::DUMPCHEST:
            std::cout << "Dump chest";
            break;
        case Player::Mode::TRAP:
            std::cout << "Trap";
            break;
        case Player::Mode::MINE:
            std::cout << "Mine";
            break;
    }
    std::cout << "      ";
    cursor.goTo(10, WIDTH + 10);
    sista::setAttribute(sista::Attribute::BRIGHT);
    std::cout << "Time survived: " << i << "    \n";
    sista::resetAttribute(sista::Attribute::BRIGHT);
    cursor.goTo(11, WIDTH + 10);

    if (i == 0) printKeys();
}
void printKeys() {
    cursor.goTo(12, WIDTH + 10);
    sista::setAttribute(sista::Attribute::BRIGHT);
    std::cout << "Instructions\n";
    sista::resetAttribute(sista::Attribute::BRIGHT);
    cursor.goTo(13, WIDTH + 10);
    std::cout << "Move: \x1b[35mw\x1b[37m | \x1b[35ma\x1b[37m | \x1b[35ms\x1b[37m | \x1b[35md\x1b[37m\n";
    cursor.goTo(14, WIDTH+10);
    std::cout << "Act: \x1b[35mi\x1b[37m | \x1b[35mj\x1b[37m | \x1b[35mk\x1b[37m | \x1b[35ml\x1b[37m\n";
    cursor.goTo(16, WIDTH + 10);
    std::cout << "Collect mode: \x1b[35mc\x1b[37m\n";
    cursor.goTo(17, WIDTH + 10);
    std::cout << "Bullet mode: \x1b[35mb\x1b[37m\n";
    cursor.goTo(18, WIDTH + 10);
    std::cout << "Dump Chest mode: \x1b[35me\x1b[37m\n";
    cursor.goTo(19, WIDTH + 10);
    std::cout << "Place Trap mode: \x1b[35mt\x1b[37m\n";
    cursor.goTo(20, WIDTH + 10);
    std::cout << "Place Mine mode: \x1b[35mm\x1b[37m | \x1b[35m*\x1b[37m\n";
    cursor.goTo(22, WIDTH + 10);
    std::cout << "Speedup mode: \x1b[35m+\x1b[37m | \x1b[35m-\x1b[37m\n";
    cursor.goTo(24, WIDTH + 10);
    std::cout << "Quit: \x1b[35mQ\x1b[37m\n";
}
void reprint() {
    sista::clearScreen(true);
    field->print(border);
    printKeys();
}

void generateTunnels() {
    std::uniform_int_distribution<int> distr(TUNNEL_UNIT * 2, WIDTH-(TUNNEL_UNIT * 2)-1); // Inclusive
    int portalCoordinate;
    for (int row=0; row<HEIGHT; row++) {
        if (row % (TUNNEL_UNIT * 3) == 0 && row + TUNNEL_UNIT * 3 < HEIGHT) {
            for (int i = 0; i < PORTALS_PER_LINE; i++) {
                sista::Coordinates abovePortalCoordinates;
                sista::Coordinates belowPortalCoordinates;
                do {
                    portalCoordinate = distr(rng);
                    abovePortalCoordinates = {row + TUNNEL_UNIT * 2, portalCoordinate};
                    belowPortalCoordinates = {row + TUNNEL_UNIT * 3 - 1, portalCoordinate};
                } while (!field->isFree(abovePortalCoordinates) || !field->isFree(belowPortalCoordinates));
                auto abovePortal = std::make_shared<Portal>(abovePortalCoordinates);
                auto belowPortal = std::make_shared<Portal>(belowPortalCoordinates);
                abovePortal->exit = belowPortal;
                belowPortal->exit = abovePortal;
                Portal::portals.push_back(abovePortal);
                Portal::portals.push_back(belowPortal);
                field->addPawn(abovePortal);
                field->addPawn(belowPortal);
            }
        }

        if (row % (TUNNEL_UNIT * 3) >= TUNNEL_UNIT * 2) { // Every two units skipped, one is built
            for (int column=0; column<WIDTH; column++) {
                if (column < TUNNEL_UNIT * 2
                    && (row / TUNNEL_UNIT / 3) % 2 == 0) {
                    // On "even" horizontal tunnels we leave tunnel space on the left
                    column = TUNNEL_UNIT * 2;
                    if (row % (TUNNEL_UNIT * 3) == TUNNEL_UNIT * 3 - 1) {
                        passages[row] = std::vector<int>(TUNNEL_UNIT * 2);
                        std::iota(
                            passages[row].begin(),
                            passages[row].end(), 0
                        );
                        breaches[row] = {};
                    } // One of the breaches in the wall is always the built-in one
                } else if (column >= WIDTH-(TUNNEL_UNIT * 2)
                            && (row / TUNNEL_UNIT / 3) % 2 == 1) {
                    // On "odd" horizontal tunnels we leave tunnel space on the right
                    if (row % (TUNNEL_UNIT * 3) == TUNNEL_UNIT * 3 - 1) {
                        passages[row] = std::vector<int>(TUNNEL_UNIT * 2);
                        std::iota(
                            passages[row].begin(),
                            passages[row].end(),
                            WIDTH - (TUNNEL_UNIT * 2)
                        );
                        breaches[row] = {};
                    } // One of the breaches in the wall is always the built-in one
                    break; // On "odd" horizontal tunnels we leave tunnel space on the right
                }
                if (field->isFree((unsigned short)row, (unsigned short)column)) {
                    auto wcoords = sista::Coordinates((unsigned short)row, (unsigned short)column);
                    auto w = std::make_shared<Wall>(wcoords, INITIAL_WALL_STRENGTH - row / TUNNEL_UNIT / 3);
                    Wall::walls.push_back(w);
                    field->addPawn(w);
                }
            }
        }
    }
}

void spawnInitialEnemies() {
    std::deque<sista::Coordinates> freeBaseCoordinates;
    for (int column = 0; column < WIDTH; column++) {
        sista::Coordinates coords = {HEIGHT - 1, column};
        if (field->isFree(coords)) {
            freeBaseCoordinates.push_back({HEIGHT - 1, column});
        }
    }
    std::shuffle(freeBaseCoordinates.begin(), freeBaseCoordinates.end(), rng);

    for (int i = 0; i < INITIAL_ARCHERS; i++) {
        auto a = std::make_shared<Archer>(freeBaseCoordinates.front());
        Archer::archers.push_back(a);
        field->addPawn(a);
        freeBaseCoordinates.pop_front();
    }
    for (int i = 0; i < INITIAL_WORMS; i++) {
        auto w = std::make_shared<Worm>(freeBaseCoordinates.front(), Direction::UP);
        Worm::worms.push_back(w);
        field->addPawn(w);
        freeBaseCoordinates.pop_front();
    }
}

void spawnEnemies() {
    std::uniform_int_distribution<int> columnDistribution(0, WIDTH - 1);
    if (Archer::spawning(rng)) {
        sista::Coordinates coords = {HEIGHT - 1, (unsigned short)columnDistribution(rng)};
        if (field->isFree(coords)) {
            auto a = std::make_shared<Archer>(coords);
            Archer::archers.push_back(a);
            field->addPrintPawn(a);
        }
    }
    if (Worm::spawning(rng)) {
        sista::Coordinates coords = {HEIGHT - 1, (unsigned short)columnDistribution(rng)};
        if (field->isFree(coords)) {
            auto w = std::make_shared<Worm>(coords, Direction::UP);
            Worm::worms.push_back(w);
            field->addPrintPawn(w);
        }
    }
}

/** Takes input and processes the moves.
 * @param io the handler for the input and output sources
 * @note this function is meant to run in a separate thread
 * @note the InavjagaIO object handles the mutex locks on its own
 */
template<typename IO>
void input(IO* io) {
    MoveEvent moveEvent = {INAVJAGA_PLAYER_ID_IGNORE, INAVJAGA_CHAR_MOVE_IGNORE};
    while (!end) {
        moveEvent = io->getMove();
        if (moveEvent.playerId == INAVJAGA_PLAYER_ID_IGNORE) {
            moveEvent.playerId = Player::localPlayerId;
        }
        if (end) return;
        if (moveEvent.move == INAVJAGA_CHAR_MOVE_IGNORE) {
            continue;
        }
        if (isAct(moveEvent)) {
            std::unique_lock lock(movesBufferMutex);
            movesBuffer.push(moveEvent);
            #if CLIENT
            if (std::is_same<IO, RemoteInavjagaIO>::value) continue;
            #endif
            io->sendMove(moveEvent);
        }
    }
}

bool isAct(MoveEvent event) {
    static std::set<char> moves = std::set<char>({
        'w', 'a', 's', 'd',
        'j', 'k', 'l', 'i',
        'c', 'b', 'e', 'm', 'q'
    });
    char move = std::tolower(event.move);
    return moves.count(move) > 0;
}

/** Process an action to the field from a Player
 * \param event the move event to process
 * \return whether the event was a valid one
 */
bool act(MoveEvent event) {
    std::shared_ptr<Player> player = Player::players[event.playerId];
    switch (event.move) {
        case 'w': case 'W': {
            std::scoped_lock<std::mutex> lock(streamMutex);
            player->move(Direction::UP);
            break;
        }
        case 'a': case 'A': {
            std::scoped_lock<std::mutex> lock(streamMutex);
            player->move(Direction::LEFT);
            break;
        }
        case 's': case 'S': {
            std::scoped_lock<std::mutex> lock(streamMutex);
            player->move(Direction::DOWN);
            break;
        }
        case 'd': case 'D': {
            std::scoped_lock<std::mutex> lock(streamMutex);
            player->move(Direction::RIGHT);
            break;
        }

        case 'j': case 'J': {
            std::scoped_lock<std::mutex> lock(streamMutex);
            player->shoot(Direction::LEFT);
            break;
        }
        case 'k': case 'K': {
            std::scoped_lock<std::mutex> lock(streamMutex);
            player->shoot(Direction::DOWN);
            break;
        }
        case 'l': case 'L': {
            std::scoped_lock<std::mutex> lock(streamMutex);
            player->shoot(Direction::RIGHT);
            break;
        }
        case 'i': case 'I': {
            std::scoped_lock<std::mutex> lock(streamMutex);
            player->shoot(Direction::UP);
            break;
        }

        case 'c': case 'C':
            player->mode = Player::Mode::COLLECT;
            break;
        case 'b': case 'B':
            player->mode = Player::Mode::BULLET;
            break;
        case 'e': case 'E': case 'q':
            player->mode = Player::Mode::DUMPCHEST;
            break;
        case 'm': case 'M': case '*':
            player->mode = Player::Mode::MINE;
            break;

        case 'Q': /* case 'q': */
            if (event.playerId == Player::localPlayerId) {
                printEndInformation(EndReason::QUIT);
                Player::localPlayer->dead = true;
                end = true;
            } else {
                Player::disconnectPlayer(event.playerId);
            }
            break;
        default:
            return false;
    }
    return isAct(event);
}

bool act(char input_) {
    return act(MoveEvent{Player::localPlayerId, input_});
}

void printEndInformation(EndReason endReason) {
    cursor.goTo(HEIGHT, WIDTH + 10);
    
    sista::resetAnsi();
    sista::setAttribute(sista::Attribute::BLINK);
    switch (endReason) {
        case EndReason::EATEN:
            std::cout << "You have been eaten by a ";
            Worm::wormHeadStyle.apply();
            sista::setAttribute(sista::Attribute::BLINK);
            std::cout << "WORM";
            break;
        case EndReason::QUIT:
            std::cout << "You quit the game with the capital 'Q' key";
            break;
        case EndReason::SHOT:
            std::cout << "You have been shot with a ";
            EnemyBullet::enemyBulletStyle.apply();
            sista::setAttribute(sista::Attribute::BLINK);
            std::cout << "BULLET";
            break;
        case EndReason::STABBED:
            std::cout << "You have been stabbed by an ";
            Archer::archerStyle.apply();
            sista::setAttribute(sista::Attribute::BLINK);
            std::cout << "ARCHER";
            break;
        case EndReason::STARVED:
            std::cout << "You have starved because you ran out of meat";
            break;
        case EndReason::TOUCHDOWN:
            std::cout << "Some enemy reached the top right area";
            break;
        default:
            std::cout << "Something unexpected went wrong internally";
            break;
    }
    std::cout << std::flush;
}

#if CLIENT
/** @brief Sets the local constants to the values passed as parameter
 * @param constants The constants to be set
 * @todo Add error handling around std::get<T> or maybe just go for std::holds_alternative<T>
 */
void setConstantsToReceivedValues(const std::map<std::string, std::variant<int, float>>& constants) {
    WIDTH = std::get<int>(constants.at("WIDTH"));
    HEIGHT = std::get<int>(constants.at("HEIGHT"));
    TUNNEL_UNIT = std::get<int>(constants.at("TUNNEL_UNIT"));
    PORTALS_PER_LINE = std::get<int>(constants.at("PORTALS_PER_LINE"));
    FRAME_DURATION = std::get<int>(constants.at("FRAME_DURATION"));
    BULLET_SPEED = std::get<int>(constants.at("BULLET_SPEED"));
    DROP_INVENTORY_ON_DEATH = std::get<int>(constants.at("DROP_INVENTORY_ON_DEATH"));
    INITIAL_CLAY = std::get<int>(constants.at("INITIAL_CLAY"));
    INITIAL_BULLETS = std::get<int>(constants.at("INITIAL_BULLETS"));
    INITIAL_MEAT = std::get<int>(constants.at("INITIAL_MEAT"));
    INITIAL_INVENTORY = {
        INITIAL_CLAY,
        INITIAL_BULLETS,
        INITIAL_MEAT
    };
    LOOT_ARCHER_CLAY = std::get<int>(constants.at("LOOT_ARCHER_CLAY"));
    LOOT_ARCHER_BULLETS = std::get<int>(constants.at("LOOT_ARCHER_BULLETS"));
    LOOT_ARCHER_MEAT = std::get<int>(constants.at("LOOT_ARCHER_MEAT"));
    LOOT_WORM_HEAD_CLAY = std::get<int>(constants.at("LOOT_WORM_HEAD_CLAY"));
    LOOT_WORM_HEAD_BULLETS = std::get<int>(constants.at("LOOT_WORM_HEAD_BULLETS"));
    LOOT_WORM_HEAD_MEAT = std::get<int>(constants.at("LOOT_WORM_HEAD_MEAT"));
    COST_OF_MINE_CLAY = (short)std::get<int>(constants.at("COST_OF_MINE_CLAY"));
    COST_OF_MINE_BULLETS = (short)std::get<int>(constants.at("COST_OF_MINE_BULLETS"));
    COST_OF_MINE_MEAT = (short)std::get<int>(constants.at("COST_OF_MINE_MEAT"));
    MEAT_DURATION_PERIOD = std::get<int>(constants.at("MEAT_DURATION_PERIOD"));
    SPAWN_COORDINATES_Y = (unsigned short)std::get<int>(constants.at("SPAWN_COORDINATES_Y"));
    SPAWN_COORDINATES_X = (unsigned short)std::get<int>(constants.at("SPAWN_COORDINATES_X"));
    RESPAWN_COORDINATES_Y = (unsigned short)std::get<int>(constants.at("RESPAWN_COORDINATES_Y"));
    RESPAWN_COORDINATES_X = (unsigned short)std::get<int>(constants.at("RESPAWN_COORDINATES_X"));
    MINE_MINIMUM_DAMAGE = std::get<int>(constants.at("MINE_MINIMUM_DAMAGE"));
    MINE_MAXIMUM_DAMAGE = std::get<int>(constants.at("MINE_MAXIMUM_DAMAGE"));
    MINE_SENSITIVITY_RADIUS = std::get<int>(constants.at("MINE_SENSITIVITY_RADIUS"));
    MINE_DAMAGE_RADIUS = std::get<int>(constants.at("MINE_DAMAGE_RADIUS"));
    Mine::explosion = std::bernoulli_distribution(MINE_EXPLOSION_IN_FRAME_PROBABILITY);
    Mine::mineDamage = std::uniform_int_distribution<int>(MINE_MINIMUM_DAMAGE, MINE_MAXIMUM_DAMAGE);
    INITIAL_WALL_STRENGTH = std::get<int>(constants.at("INITIAL_WALL_STRENGTH"));
    WORM_HEALTH_POINTS = std::get<int>(constants.at("WORM_HEALTH_POINTS"));
    WALL_WEARING_PROBABILITY = std::get<float>(constants.at("WALL_WEARING_PROBABILITY"));
    Wall::wearing = std::bernoulli_distribution(WALL_WEARING_PROBABILITY);
    DAMAGED_WALLS_COUNT = std::get<int>(constants.at("DAMAGED_WALLS_COUNT"));
    MINE_EXPLOSION_IN_FRAME_PROBABILITY = std::get<float>(constants.at("MINE_EXPLOSION_IN_FRAME_PROBABILITY"));
    DUMB_MOVE_PROBABILITY = std::get<float>(constants.at("DUMB_MOVE_PROBABILITY"));
    dumbMoveDistribution = std::bernoulli_distribution(DUMB_MOVE_PROBABILITY);
    ARCHER_SPAWNING_PROBABILITY = std::get<float>(constants.at("ARCHER_SPAWNING_PROBABILITY"));
    ARCHER_MOVING_PROBABILITY = std::get<float>(constants.at("ARCHER_MOVING_PROBABILITY"));
    ARCHER_SHOOTING_PROBABILITY = std::get<float>(constants.at("ARCHER_SHOOTING_PROBABILITY"));
    Archer::moving = std::bernoulli_distribution(ARCHER_MOVING_PROBABILITY);
    Archer::shooting = std::bernoulli_distribution(ARCHER_SHOOTING_PROBABILITY);
    Archer::spawning = std::bernoulli_distribution(ARCHER_SPAWNING_PROBABILITY);
    WORM_TURNING_PROBABILITY = std::get<float>(constants.at("WORM_TURNING_PROBABILITY"));
    WORM_SPAWNING_PROBABILITY = std::get<float>(constants.at("WORM_SPAWNING_PROBABILITY"));
    WORM_EATING_ARCHER_PROBABILITY = std::get<float>(constants.at("WORM_EATING_ARCHER_PROBABILITY"));
    WORM_EATING_TAIL_PROBABILITY = std::get<float>(constants.at("WORM_EATING_TAIL_PROBABILITY"));
    WORM_MOVING_PROBABILITY = std::get<float>(constants.at("WORM_MOVING_PROBABILITY"));
    CLAY_RELEASE_PROBABILITY = std::get<float>(constants.at("CLAY_RELEASE_PROBABILITY"));
    Worm::turning = std::bernoulli_distribution(WORM_TURNING_PROBABILITY);
    Worm::moving = std::bernoulli_distribution(WORM_MOVING_PROBABILITY);
    Worm::spawning = std::bernoulli_distribution(WORM_SPAWNING_PROBABILITY);
    Worm::eatingTail = std::bernoulli_distribution(WORM_EATING_TAIL_PROBABILITY);
    Worm::eatingArcher = std::bernoulli_distribution(WORM_EATING_ARCHER_PROBABILITY);
    Worm::clayRelease = std::bernoulli_distribution(CLAY_RELEASE_PROBABILITY);
    INITIAL_ARCHERS = std::get<int>(constants.at("INITIAL_ARCHERS"));
    INITIAL_WORMS = std::get<int>(constants.at("INITIAL_WORMS"));
    WORM_LENGTH = std::get<int>(constants.at("WORM_LENGTH"));
}
#endif

/** @brief Places the client's player at the coordinates negotiated with the server
 * @param connectionToServer the connection over which to negotiate with the server
 * @note by default the respawnCoordinates of said Player will be set to the spawn ones
 */
void placeClientPlayer(std::shared_ptr<ClientInavjagaGSPIO> connectionToServer) {
    sista::Coordinates spawn = negotiateCoordinates(field, connectionToServer);
    Player::localPlayer = std::make_shared<Player>(spawn);
    Player::localPlayer->respawnCoordinates = spawn;
    Player::localPlayer->mode = Player::Mode::BULLET;
    field->addPawn(Player::localPlayer);
}

/** @brief Negotiates the spawn coordinates with the client
 * @param field_ The field on which to place the player of this client
 * @param client The connection to the client to negotiate with
 * @retval {SPAWN_COORDINATES_Y, SPAWN_COORDINATES_X} if no coordinates agreed upon
 * @return The spawn coordinates agreed upon with the client
 */
sista::Coordinates negotiateCoordinates(std::weak_ptr<sista::SwappableField> field_, std::shared_ptr<ServerInavjagaGSPIO> client) {
    sista::Coordinates candidate{SPAWN_COORDINATES_Y, SPAWN_COORDINATES_X};
    // The < condition is actually a >=, it's just that we are dealing with unsigned integers
    for (unsigned short y = TUNNEL_UNIT * 2 - 1; y < TUNNEL_UNIT * 2; y--) {
        for (unsigned short x = WIDTH - 1; x < WIDTH; x--) {
            candidate = {y, x};
            if (field_.lock()->isFree(candidate)) {
                #if DEBUG
                {
                    std::unique_lock lock(stderrMutex);
                    std::cerr << "Offering {" << y << ", " << x << "}" << std::endl;
                }
                #endif
                if (client->offerCoordinates(candidate)) {
                    return candidate;
                }
            }
        }
    }
    return candidate;
}

/** @brief Negotiates the clien'ts own spawn coordinates with the server
 * @param field_ The field on which to place the player of this client
 * @param server The connection to the server to negotiate with
 * @retval {SPAWN_COORDINATES_Y, SPAWN_COORDINATES_X} if no coordinates agreed upon
 * @return The spawn coordinates agreed upon with the server
 */
sista::Coordinates negotiateCoordinates(std::weak_ptr<sista::SwappableField> field_, std::shared_ptr<ClientInavjagaGSPIO> server) {
    sista::Coordinates candidate;
    while (true) {
        candidate = server->recvCoordinates();
        if (candidate.y >= TUNNEL_UNIT * 2 || field_.lock()->isOutOfBounds(candidate)) {
            server->sendNo();
        } else {
            server->sendYes();
            break;
        }
    }
    return candidate;
}

void deallocateAll() {
    Wall::walls.clear();
    Bullet::bullets.clear();
    Chest::chests.clear();
    Portal::portals.clear();
    Mine::mines.clear();
    EnemyBullet::enemyBullets.clear();
    Archer::archers.clear();
    WormBody::wormBodies.clear();
    Worm::worms.clear();
}

std::unordered_map<Direction, sista::Coordinates> directionMap = {
    {Direction::UP, {(unsigned short)-1, 0}},
    {Direction::RIGHT, {0, 1}},
    {Direction::DOWN, {1, 0}},
    {Direction::LEFT, {0, (unsigned short)-1}}
};
std::unordered_map<Direction, char> directionSymbol = {
    {Direction::UP, '^'},
    {Direction::RIGHT, '>'},
    {Direction::DOWN, 'v'},
    {Direction::LEFT, '<'}
};
std::set<char> movementKeys = {
    'w', 'W', 'd', 'D', 's', 'S', 'a', 'A'
};
std::random_device randomDevice;
std::mt19937 rng;
std::map<int, std::vector<int>> passages; // Lateral passages, "main tunnel" tresholds
std::map<int, std::vector<int>> breaches; // Central breaches, "holes"
