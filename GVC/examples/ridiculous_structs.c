// Ridiculous struct stress test for DoomGVC
// This file intentionally mixes valid and edge-case struct uses.

struct Vec2 {
    int x;
    int y;
};

struct Inner {
    int a;
    int b;
};

struct Outer {
    struct Inner in;
    int c;
};

struct Big {
    char c;
    int i;
    long long ll;
    struct Vec2 v;
};

struct Node {
    int value;
    struct Node* next;
};

int sum_vec(struct Vec2* p) {
    return p->x + p->y;
}

void init_node(struct Node* node, int value) {
    node->value = value;
    node->next = 0;
}

void link_nodes(struct Node* left, struct Node* right) {
    left->next = right;
}

int sum_list(struct Node* head) {
    int total;
    total = 0;

    while (head) {
        total = total + head->value;
        head = head->next;
    }

    return total;
}

int main() {
    struct Vec2 v;
    struct Vec2* pv;
    struct Outer o;
    struct Big b;
    struct Node n1;
    struct Node n2;
    struct Node n3;
    int list_total;

    // Basic field writes
    v.x = 10;
    v.y = 20;

    // Pointer member access
    pv = &v;
    pv->x = pv->x + 1;

    // Nested member chains
    o.in.a = 3;
    o.in.b = 4;
    o.c = 5;

    // Struct member that is another struct
    b.c = 'A';
    b.i = 7;
    b.ll = 100;
    b.v.x = 11;
    b.v.y = 22;

    // sizeof madness
    int s1;
    int s2;
    int s3;
    int s4;
    s1 = sizeof(struct Vec2);
    s2 = sizeof(struct Outer);
    s3 = sizeof(struct Big);
    s4 = sizeof(struct Vec2*);

    // Linked list built from stack nodes
    init_node(&n1, 9);
    init_node(&n2, 8);
    init_node(&n3, 7);
    link_nodes(&n1, &n2);
    link_nodes(&n2, &n3);
    list_total = sum_list(&n1);

    // Return a deterministic value to quickly compare behavior
    return sum_vec(pv) + o.in.a + o.in.b + o.c + b.v.x + b.v.y + s1 + s2 + s3 + s4 + list_total;
}
