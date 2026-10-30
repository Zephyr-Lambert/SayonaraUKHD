/* Build-time tool only; compile with MinGW and run on Windows or Wine. */
#define COBJMACROS
#include <windows.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: compile_shaders fxaa.hlsl fxaa_bytecode.h\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *source = malloc(n + 1);
    if (!source || fread(source, 1, n, f) != (size_t)n)
        return 3;
    source[n] = 0;
    fclose(f);
    FILE *out = fopen(argv[2], "wb");
    if (!out)
        return 4;
    fprintf(out, "/* Generated from fxaa.hlsl by compile_shaders.c. */\n");
    const char *names[] = {"fxaaVS", "fxaaPS"}, *entry[] = {"VSMain", "PSMain"},
               *target[] = {"vs_4_0", "ps_4_0"};
    for (unsigned k = 0; k < 2; k++) {
        ID3DBlob *code = NULL, *errors = NULL;
        HRESULT hr = D3DCompile(source, n, argv[1], NULL, NULL, entry[k], target[k],
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (errors) {
            fwrite(ID3D10Blob_GetBufferPointer(errors), 1, ID3D10Blob_GetBufferSize(errors), stderr);
            ID3D10Blob_Release(errors);
        }
        if (FAILED(hr)) {
            fprintf(stderr, "compile failed %08lx\n", (unsigned long)hr);
            fclose(out);
            return 5;
        }
        unsigned char *b = ID3D10Blob_GetBufferPointer(code);
        size_t size = ID3D10Blob_GetBufferSize(code);
        fprintf(out, "static const unsigned char %s[] = {\n", names[k]);
        for (size_t i = 0; i < size; i++)
            fprintf(out, "0x%02x,%s", b[i], (i % 16 == 15) ? "\n" : "");
        fprintf(out, "\n};\n");
        printf("%s %zu bytes\n", target[k], size);
        ID3D10Blob_Release(code);
    }
    fclose(out);
    free(source);
    return 0;
}
