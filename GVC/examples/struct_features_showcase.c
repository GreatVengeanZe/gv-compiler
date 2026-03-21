// Comprehensive showcase of struct/union features implemented in GVC

extern int printf(char* s, ...);

// Struct with various member types
struct Person {
    char name[20];
    int age;
    int scores[3];        // Array member
};

// Struct with bit fields (parsing works, codegen pending)
struct Options {
    unsigned int enabled : 1;
    unsigned int debug : 1;
    unsigned int verbose : 2;
    unsigned int padding : 28;
};

// Union with array member
union Data {
    int values[4];
    char bytes[16];
};

// Nested struct
struct Employee {
    struct Person person;
    int salary;
    union Data work_data;
};

int main() {
    // Initialize a person with an array member
    struct Person p1;
    p1.scores[0] = 95;
    p1.scores[1] = 87;
    p1.scores[2] = 92;
    p1.age = 25;
    
    printf("Person scores: %d, %d, %d\n", p1.scores[0], p1.scores[1], p1.scores[2]);
    
    // Union with array member
    union Data d;
    d.values[0] = 100;
    d.values[1] = 200;
    d.values[2] = 300;
    d.values[3] = 400;
    
    printf("Union data: %d, %d, %d, %d\n", 
           d.values[0], d.values[1], d.values[2], d.values[3]);
    
    // Nested struct
    struct Employee e;
    e.person.age = 35;
    e.person.scores[0] = 100;
    e.salary = 50000;
    e.work_data.values[0] = 42;
    
    printf("Employee age=%d, score=%d, salary=%d, work=%d\n",
           e.person.age, e.person.scores[0], e.salary, e.work_data.values[0]);
    
    return 0;
}
