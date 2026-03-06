extern int printf(char* s, ...);

/* Normal enum */
enum Day {
    MONDAY,
    TUESDAY,
    WEDNESDAY
};

/* Enum with strange numbering */
enum WeirdNumbers {
    NEGATIVE = -100,
    ZERO = 0,
    HUGE = 999999,
    ALSO_HUGE
};

/* Enum used as boolean replacement */
enum Truth {
    FALSE,
    TRUE
};

enum myBool {
    false = 0,
    true = !false
};

/* Enum used as ASCII characters */
enum Characters {
    LETTER_A = 'A',
    LETTER_B = 'B',
    LETTER_C = 'C'
};

/* Enum used as array indexes */
enum ArrayIndex {
    FIRST,
    SECOND,
    THIRD,
    ARRAY_SIZE
};

/* Enum used as bit flags */
enum SuperPowers {
    FLYING      = 1 << 0,
    INVISIBLE   = 1 << 1,
    LASER_EYES  = 1 << 2,
    EATING_PIZZA = 1 << 3
};

/* Completely absurd enum values */
enum Ridiculous {
    BANANA = 1000000,
    UNIVERSE = 42,
    POTATO = -7,
    TIME_TRAVEL = 123456789
};

int main() {

    /* ===== Basic enum usage ===== */
    enum Day today = WEDNESDAY;
    printf("Today = %d\n", today);

    /* ===== Weird numbering ===== */
    printf("NEGATIVE = %d\n", NEGATIVE);
    printf("HUGE = %d\n", HUGE);
    printf("ALSO_HUGE = %d\n", ALSO_HUGE);

    /* ===== Enum used as boolean ===== */
    enum Truth computer_is_confused = TRUE;

    if (computer_is_confused) {
        printf("Computer is confused.\n");
    }

    enum myBool t = false;
    if(t)
    {
       printf("Works.\n");
    }

    /* ===== Enum used as characters ===== */
    printf("Letters: %c %c %c\n", LETTER_A, LETTER_B, LETTER_C);

    /* ===== Enum used as array size/index ===== */
    int numbers[ARRAY_SIZE] = {10, 20, 30};

    printf("numbers[FIRST] = %d\n", numbers[FIRST]);
    printf("numbers[SECOND] = %d\n", numbers[SECOND]);
    printf("numbers[THIRD] = %d\n", numbers[THIRD]);

    /* ===== Enum used as bit flags ===== */
    int hero = FLYING | LASER_EYES;

    if (hero & FLYING) {
        printf("Hero can fly\n");
    }

    if (hero & LASER_EYES) {
        printf("Hero has laser eyes\n");
    }

    /* ===== Completely ridiculous values ===== */
    printf("BANANA = %d\n", BANANA);
    printf("UNIVERSE = %d\n", UNIVERSE);
    printf("POTATO = %d\n", POTATO);
    printf("TIME_TRAVEL = %d\n", TIME_TRAVEL);

    /* ===== Assigning an invalid enum value (still legal in C) ===== */
    enum Day strange_day = 999;   // perfectly allowed in C
    printf("Strange day = %d\n", strange_day);

    return 0;
}