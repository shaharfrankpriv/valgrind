#define arr_sz (203)
int array[arr_sz];

int main(int argc, char **argv)
{
    int sum = 0;
    for (int j = 0; j < 80; j++) {
    for (int i = 0; i < arr_sz; i++) {
        array[3] = i;
    }
    for (int i = 0; i < arr_sz; i++) {
        sum += array[3];
    }
    }
    return sum % 128;
}
