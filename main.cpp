#include <bits/stdc++.h>

/// 根据每个比特关于它之前COR_BIT个比特的条件概率，进行熵编码。动态更新概率，更新速率为RATE，按比例更新，最小概率渐近于MIN_PROB
const int COR_BIT = 26; /// 相关比特数，取值[0, 27]，如果内存够大理论上无上限。该数值并非越大越好，看数据的关联性
double RATE = .7; /// 概率模型学习率，取值[0.01, 1]，一般取接近1的值
double MIN_PROB = .00001; /// 最小概率限制，取值(0, 0.5)，一般取接近0的值但太小时会有精度问题，大文件会出问题


double prob[1 << COR_BIT];
int window;
uint64_t left;
uint64_t right;
int E3_count;

uint64_t codec_buf;
int buf_len;
int read_count;
char read_buf[4096];
int write_count;
char write_buf[4096];

int pos1;
int pos2;
int dec_end;

unsigned seed = 0; /// 初始概率模型的随机种子，用于加密压缩

void init() {
    if (seed != 0) {
        srand(seed);
        for (int i = 0; i < (1 << COR_BIT); i++) {
            prob[i] = .5 + .01 * sin(rand());
        }
    } else {
        for (int i = 0; i < (1 << COR_BIT); i++) {
            prob[i] = .5;
        }
    }
    window = 0;
    left = 0;
    right = UINT64_MAX;
    E3_count = 0;
    codec_buf = 0;
    buf_len = 0;
    pos1 = 0;
    read_count = 0;
    write_count = 0;
    dec_end = 0;
}

void update_prob(int bit) {
    prob[window] *= RATE;
    prob[window] += bit ? (MIN_PROB * (1 - RATE)) : ((1 - MIN_PROB) * (1 - RATE));
    window <<= 1;
    window |= bit;
    window &= (1 << COR_BIT) - 1;
}

void E1() {
    left <<= 1;
    right <<= 1;
    right += 1;
}

void E2() {
    left -= UINT64_MAX / 2 + 1;
    right -= UINT64_MAX / 2 + 1;
    left <<= 1;
    right <<= 1;
    right += 1;
}

void E3() {
    left -= UINT64_MAX / 4 + 1;
    right -= UINT64_MAX / 4 + 1;
    left <<= 1;
    right <<= 1;
    right += 1;
}

void out_cod(int bit, FILE *out) {
    codec_buf |= bit << buf_len;
    buf_len++;
    if (buf_len >= 8) {
        write_buf[write_count] = codec_buf;
        write_count++;
        if (write_count == 4096) {
            fwrite(write_buf, 1, 4096, out);
            write_count = 0;
        }
        buf_len = 0;
        codec_buf = 0;
    }
    while (E3_count > 0) {
        E3_count--;
        codec_buf |= (!bit) << buf_len;
        buf_len++;
        if (buf_len >= 8) {
            write_buf[write_count] = codec_buf;
            write_count++;
            if (write_count == 4096) {
                fwrite(write_buf, 1, 4096, out);
                write_count = 0;
            }
            buf_len = 0;
            codec_buf = 0;
        }
    }
}

void encode(FILE *in, FILE *out) {
    init();
    fputc(0, out);
    int origin_len = 0;
    int cod_len = 0;
    while ((read_count = fread(read_buf, 1, 4096, in)) > 0) {
        origin_len += read_count;
        for (int i = 0; i < read_count; i++) {
            int c = read_buf[i];
            for (int j = 0; j < 8; j++) {
                int bit = c & 1;
                c >>= 1;
                uint64_t len = right - left;
                if (!bit) right = left + (uint64_t) (len * prob[window]);
                else left += (uint64_t) (len * prob[window]) + 1;
                while (1) {
                    if (right <= UINT64_MAX / 2) {
                        E1();
                        cod_len++;
                        out_cod(0, out);
                    } else if (left > UINT64_MAX / 2) {
                        E2();
                        cod_len++;
                        out_cod(1, out);
                    } else if (left > UINT64_MAX / 4 && right <= (uint64_t) UINT64_MAX * 3 / 4) {
                        E3();
                        cod_len++;
                        E3_count++;
                    } else break;
                }
                update_prob(bit);
            }
        }
    }
    cod_len++;
    out_cod(1, out);
    write_buf[write_count] = codec_buf;
    write_count++;
    fwrite(write_buf, 1, write_count, out);
    rewind(out);
    fputc(origin_len % 256, out);
    cod_len = cod_len / 8 + 2;
    printf("Original length = %d\nEncode length = %d\n", origin_len, cod_len);
}

int get_dec_bit(FILE *in) {
    if (pos1 >= read_count) {
        read_count = fread(read_buf, 1, 4096, in);
        if (read_count <= 0) {
            dec_end = 1;
            return 0;
        }
        pos1 = 0;
        pos2 = 0;
    }
    pos2++;
    int bit = read_buf[pos1] & 1;
    read_buf[pos1] >>= 1;
    if (pos2 >= 8) {
        pos2 = 0;
        pos1++;
    }
    return bit;
}

void decode(FILE *in, FILE *out) {
    init();
    int origin_len_mod256 = fgetc(in);
    uint64_t dec_window = 0;
    for (int i = 0; i < 64; i++) {
        dec_window <<= 1;
        dec_window |= get_dec_bit(in);
    }
    while (1) {
        uint64_t len = right - left;
        int bit = dec_window >=  left + (uint64_t) (len * prob[window]);
        if (!bit) right = left + (uint64_t) (len * prob[window]);
        else left += (uint64_t) (len * prob[window]) + 1;
        while (1) {
            if (right <= UINT64_MAX / 2) {
                E1();
                dec_window <<= 1;
                dec_window |= get_dec_bit(in);
            } else if (left > UINT64_MAX / 2) {
                E2();
                dec_window <<= 1;
                dec_window |= get_dec_bit(in);
            } else if (left > UINT64_MAX / 4 && right <= (uint64_t) UINT64_MAX * 3 / 4) {
                E3();
                dec_window <<= 1;
                dec_window ^= 1ULL << 63;
                dec_window |= get_dec_bit(in);
            } else break;
        }
        update_prob(bit);
        codec_buf |= bit << buf_len;
        buf_len++;
        if (buf_len == 8) {
            write_buf[write_count] = codec_buf;
            write_count++;
            if (write_count == 4096) {
                fwrite(write_buf, 1, 4096, out);
                write_count = 0;
            }
            buf_len = 0;
            codec_buf = 0;
        }
        if (dec_end && write_count % 256 == origin_len_mod256) break;
    }
    fwrite(write_buf, 1, write_count, out);
}

int main() {
    printf("ZHN Compression(CABAC) Version.2021-12-25\n");
    printf("1 - Compress\n2 - Decompress\n");
    int c;
    scanf("%d", &c);
    printf("Set seed(uint32), 0 for None\n");
    scanf("%u", &seed);
    printf("Input filename\n");
    char *filename = (char *) malloc(200);
    scanf("%s", filename);
    char *filename2 = (char *) malloc(200);
    FILE *in, *out;
    uint64_t time0 = time(NULL);
    switch (c) {
    case 1:
        in = fopen(filename, "rb");
        filename = strcat(filename, ".zhn");
        out = fopen(filename, "wb");
        encode(in, out);
        fclose(in);
        fclose(out);
        printf("Successfully saved as %s\n", filename);
        break;
    case 2:
        if (strcmp(filename + strlen(filename) - 4, ".zhn")) {
            printf("Please choose a .zhn file\n");
            break;
        }
        in = fopen(filename, "rb");
        filename[strlen(filename) - 4] = 0;
        sprintf(filename2, "Decompressed-%s", filename);
        out = fopen(filename2, "wb");
        decode(in, out);
        fclose(in);
        fclose(out);
        printf("Successfully saved as %s\n", filename2);
        break;
    }
    printf("Time elapsed = %u\n", time(NULL) - time0);
    system("pause");
    return 0;
}
