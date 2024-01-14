# BackMeUp
## Projeto simples de arquivamento e backup em C
Projeto cria um arquivamento compactado com com gzip|bzip2|xz de um diretório passado como argumento.
#
Projeto usa as bibliotecas:
* `libarchive` -> https://libarchive.org
* `libfileutils` -> https://github.com/PedroF37/FileUtils
* `libdirutils` -> https://github.com/PedroF37/DirUtils
#
Para compilar projeto: `$ clang -Wall -Wextra --pedantic -std=c99 -larchive -lfileutils -ldirutils -o bmu bmu.c`
