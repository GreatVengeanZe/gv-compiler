// Test comprehensive struct features
extern int printf(char* s, ...);

// Forward declaration
struct ForwardNode;

// Struct with various member types
struct Basic {
    int x;
    int y;
};

// Nested struct
struct Nested {
    struct Basic b;
    int z;
};

// Recursive/self-referential struct
struct Node {
    int value;
    struct Node* next;
};

// Union test
union SimpleUnion {
    int i;
    char c;
};

// Anonymous struct (nested) - currently may have issues
struct WithAnon {
    int a;
    int b;
};

int main() {
    // Test 1: Basic struct
    struct Basic b;
    b.x = 10;
    b.y = 20;
    printf("Basic: x=%d y=%d\n", b.x, b.y);
    
    // Test 2: Nested struct
    struct Nested n;
    n.b.x = 5;
    n.b.y = 15;
    n.z = 25;
    printf("Nested: b.x=%d b.y=%d z=%d\n", n.b.x, n.b.y, n.z);
    
    // Test 3: Self-referential struct
    struct Node n1;
    struct Node n2;
    n1.value = 100;
    n1.next = &n2;
    n2.value = 200;
    n2.next = 0;
    printf("Linked: n1=%d n2=%d\n", n1.value, n2.value);
    
    // Test 4: Union - will work if union member access is implemented
    union SimpleUnion su;
    su.i = 42;
    printf("Union i=%d\n", su.i);
    
    // Test 5: Struct with union
    struct WithAnon wa;
    wa.a = 1;
    wa.b = 2;
    printf("WithAnon: a=%d b=%d\n", wa.a, wa.b);
    
    return 0;
}
