
#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <FluxGarage_RoboEyes.h>

// ==========================================
// DISPLAY AND PIN CONFIGURATION
// ==========================================

constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;

constexpr int OLED_SDA = 8;
constexpr int OLED_SCL = 9;
constexpr int TOUCH_PIN = 3;

constexpr uint8_t OLED_ADDR = 0x3C;

// ==========================================
// TIMING CONFIGURATION
// ==========================================

constexpr uint32_t SAD_AFTER_MS = 30000UL;
constexpr uint32_t SLEEP_AFTER_MS = 90000UL;

constexpr uint32_t TOUCH_WINDOW_MS = 6000UL;

constexpr uint8_t TOO_MANY_TAPS = 5;

constexpr uint32_t GRUMPY_MS = 3500UL;
constexpr uint32_t HAPPY_MS = 2600UL;
constexpr uint32_t CURIOUS_MS = 2800UL;

// ==========================================
// DISPLAY OBJECTS
// ==========================================

Adafruit_SH1106G display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1
);

RoboEyes<Adafruit_SH1106G> roboEyes(display);

// ==========================================
// PET MOODS
// ==========================================

enum PetMood : uint8_t {
    MOOD_BROWSING,
    MOOD_HAPPY,
    MOOD_CURIOUS,
    MOOD_SLEEPY,
    MOOD_GIGGLY,
    MOOD_ANGRY,
    MOOD_SAD,
    MOOD_COUNT
};

PetMood mood = MOOD_BROWSING;

// ==========================================
// STATE VARIABLES
// ==========================================

bool sleeping = false;
bool touchWasDown = false;

bool rapidWindowStarted = false;
bool reactionLocked = false;

bool curiousActive = false;
bool havePreviousTap = false;

uint8_t rapidTapCount = 0;

int8_t curiousPosition = -1;

// ==========================================
// TIMERS
// ==========================================

uint32_t lastPetAt = 0;

uint32_t tapWindowAt = 0;
uint32_t previousTapAt = 0;

uint32_t reactionStartedAt = 0;
uint32_t reactionLength = 0;

uint32_t nextMoodAt = 0;
uint32_t nextCuriousAt = 0;

uint32_t curiousStartedAt = 0;

// ==========================================
// TIME HELPERS
// ==========================================

bool elapsed(uint32_t now, uint32_t since, uint32_t duration) {
    return (uint32_t)(now - since) >= duration;
}

bool due(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

// ==========================================
// MOOD CONFIGURATION
// ==========================================

void setMood(PetMood next) {

    mood = next;

    roboEyes.setHFlicker(OFF);
    roboEyes.setVFlicker(OFF);

    roboEyes.setIdleMode(OFF);
    roboEyes.setCuriosity(OFF);

    roboEyes.setCyclops(OFF);
    roboEyes.setSweat(OFF);

    roboEyes.setAutoblinker(ON, 3, 2);

    // Rounded-square eyes
    roboEyes.setWidth(38, 38);
    roboEyes.setHeight(38, 38);
    roboEyes.setBorderradius(10, 10);

    roboEyes.setSpacebetween(8);
    roboEyes.setPosition(DEFAULT);

    switch (mood) {

        case MOOD_BROWSING:

            roboEyes.setMood(DEFAULT);
            roboEyes.setIdleMode(ON, 2, 2);
            break;

        case MOOD_HAPPY:

            roboEyes.setMood(HAPPY);
            roboEyes.setHeight(36, 36);
            break;

        case MOOD_CURIOUS:

            roboEyes.setMood(DEFAULT);
            roboEyes.setCuriosity(ON);
            break;

        case MOOD_SLEEPY:

            roboEyes.setMood(TIRED);
            roboEyes.setHeight(25, 25);
            roboEyes.setBorderradius(12, 12);
            break;

        case MOOD_GIGGLY:

            roboEyes.setMood(HAPPY);
            roboEyes.setAutoblinker(OFF);
            roboEyes.anim_laugh();
            break;

        case MOOD_ANGRY:

            roboEyes.setMood(ANGRY);
            roboEyes.setHeight(30, 30);
            roboEyes.setBorderradius(5, 5);
            roboEyes.setHFlicker(ON, 1);
            break;

        case MOOD_SAD:

            roboEyes.setMood(TIRED);
            roboEyes.setHeight(29, 29);
            roboEyes.setBorderradius(11, 11);
            roboEyes.setPosition(S);
            break;

        default:

            roboEyes.setMood(DEFAULT);
            break;
    }
}

// ==========================================
// REACTIONS
// ==========================================

void startReaction(PetMood face, uint32_t now, uint32_t duration) {

    curiousActive = false;

    setMood(face);

    reactionLocked = true;

    reactionStartedAt = now;
    reactionLength = duration;
}

// ==========================================
// SLEEP SYSTEM
// ==========================================

void enterSleep(uint32_t now) {

    sleeping = true;

    reactionLocked = false;
    curiousActive = false;

    setMood(MOOD_SLEEPY);

    roboEyes.setAutoblinker(OFF);
    roboEyes.close();

    (void)now;
}

// ==========================================
// WAKE UP SYSTEM
// ==========================================

void wakeGrumpy(uint32_t now) {

    sleeping = false;

    rapidWindowStarted = false;
    rapidTapCount = 0;

    roboEyes.open();

    startReaction(MOOD_ANGRY, now, GRUMPY_MS);

    nextMoodAt = now + random(2500, 5000);
    nextCuriousAt = now + random(7000, 14000);
}

// ==========================================
// TOUCH SENSOR
// ==========================================

void handleTouch(uint32_t now) {

    const bool touched = digitalRead(TOUCH_PIN) == HIGH;

    // Detect a new touch rather than a continuous touch
    if (touched && !touchWasDown) {

        lastPetAt = now;

        // Waking up from sleep
        if (sleeping) {

            wakeGrumpy(now);

            touchWasDown = touched;
            return;
        }

        // Rapid tap window
        if (!rapidWindowStarted ||
            elapsed(now, tapWindowAt, TOUCH_WINDOW_MS)) {

            tapWindowAt = now;
            rapidTapCount = 0;
            rapidWindowStarted = true;
        }

        if (rapidTapCount < 255) {
            ++rapidTapCount;
        }

        // Too many taps = angry
        if (rapidTapCount >= TOO_MANY_TAPS) {

            rapidTapCount = 0;
            rapidWindowStarted = false;
            havePreviousTap = false;

            startReaction(MOOD_ANGRY, now, GRUMPY_MS);
        }

        // Two quick taps = giggly
        else if (havePreviousTap &&
                 !elapsed(now, previousTapAt, 500UL)) {

            havePreviousTap = false;

            startReaction(MOOD_GIGGLY, now, HAPPY_MS);
        }

        // Normal tap = happy
        else {

            havePreviousTap = true;
            previousTapAt = now;

            startReaction(MOOD_HAPPY, now, HAPPY_MS);
        }

        nextMoodAt = now + random(3000, 7000);
        nextCuriousAt = now + random(7000, 15000);
    }

    touchWasDown = touched;
}

// ==========================================
// CURIOSITY SYSTEM
// ==========================================

void startCurious(uint32_t now) {

    curiousActive = true;

    curiousStartedAt = now;
    curiousPosition = -1;

    setMood(MOOD_CURIOUS);

    nextCuriousAt = now + random(12000, 24000);
}

void updateCuriousGaze(uint32_t now) {

    static const int8_t positions[] = {
        W, E, W, E, DEFAULT
    };

    const uint32_t age = now - curiousStartedAt;

    int8_t step =
        age < 500 ? 0 :
        age < 1000 ? 1 :
        age < 1500 ? 2 :
        age < 2000 ? 3 : 4;

    if (step != curiousPosition) {

        curiousPosition = step;

        roboEyes.setPosition(positions[step]);
    }
}

// ==========================================
// RANDOM IDLE MOODS
// ==========================================

PetMood randomIdleMood() {

    const PetMood choices[] = {
        MOOD_BROWSING,
        MOOD_HAPPY,
        MOOD_GIGGLY
    };

    PetMood pick = choices[random(0, 3)];

    if (pick == mood) {

        pick = choices[
            (uint8_t(pick) + random(1, 3)) % 3
        ];
    }

    return pick;
}

// ==========================================
// MAIN PET UPDATE
// ==========================================

void updatePet(uint32_t now) {

    if (sleeping) return;

    const uint32_t unattended = now - lastPetAt;

    // Sleep after 90 seconds
    if (unattended >= SLEEP_AFTER_MS) {

        enterSleep(now);
        return;
    }

    // Finish temporary reactions
    if (reactionLocked) {

        if (!elapsed(now, reactionStartedAt, reactionLength)) {
            return;
        }

        reactionLocked = false;
    }

    // Become sad after 30 seconds
    if (unattended >= SAD_AFTER_MS) {

        if (mood != MOOD_SAD) {
            setMood(MOOD_SAD);
        }

        curiousActive = false;
        return;
    }

    // Curiosity animation
    if (curiousActive) {

        updateCuriousGaze(now);

        if (!elapsed(now, curiousStartedAt, CURIOUS_MS)) {
            return;
        }

        curiousActive = false;

        setMood(randomIdleMood());

        nextMoodAt = now + random(3500, 9000);
        return;
    }

    // Start curiosity
    if (due(now, nextCuriousAt)) {

        startCurious(now);
        updateCuriousGaze(now);

        return;
    }

    // Random idle moods
    if (due(now, nextMoodAt)) {

        setMood(randomIdleMood());

        nextMoodAt = now + random(3500, 9000);
    }
}

// ==========================================
// SETUP
// ==========================================

void setup() {

    pinMode(TOUCH_PIN, INPUT);

    randomSeed(esp_random());

    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000);

    if (!display.begin(OLED_ADDR, true)) {

        while (true) {
            delay(100);
        }
    }

    roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 60);

    const uint32_t now = millis();

    lastPetAt = now;

    setMood(MOOD_BROWSING);

    nextMoodAt = now + random(3500, 8000);
    nextCuriousAt = now + random(6000, 12000);
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {

    const uint32_t now = millis();

    handleTouch(now);

    updatePet(now);

    roboEyes.update();
}
