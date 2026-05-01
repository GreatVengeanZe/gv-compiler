// Test French quotation marks « and » for string literals

#include <stdio.h>

int main() {
    // Test traditional double quotes
    char *traditional = "Hello with traditional quotes";
    
    // Test French quotation marks
    char *french = «Hello with French quotation marks»;
    
    // Test mixed - strings with content using both styles
    char *msg1 = "This is a normal string";
    char *msg2 = «This is a French quoted string»;
    
    // Test with escape sequences in both styles
    char *escaped1 = "Line 1\nLine 2";
    char *escaped2 = «Line 1\nLine 2»;
    
    printf("%s\n", traditional);
    printf("%s\n", french);
    printf("%s\n", msg1);
    printf("%s\n", msg2);
    printf("%s\n", escaped1);
    printf("%s\n", escaped2);
    
    return 0;
}
