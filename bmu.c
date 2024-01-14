/********************************* includes ***********************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>

#include <fileutils.h>
#include <dirutils.h>



/********************************* defines ************************************/


#define PROGNAME "bmu"
#define GZIP ARCHIVE_FILTER_GZIP
#define BZIP ARCHIVE_FILTER_BZIP2
#define XZ ARCHIVE_FILTER_XZ


/********************************* structs ************************************/


/** Append_Buffer, vai sendo concatenado com os
 * caminhos dos arquivos e diretorios, formando
 * a lista de itens para fazer o backup */
typedef struct
{
    char *buffer;
    size_t size;
}
Append_Buffer;

static Append_Buffer ab;


/********************************* protótipos *********************************/


/* Os unicos necessários */
void free_append_buffer(Append_Buffer *ab);
void cleanup();


/********************************* funções ************************************/


void cleanup()
{
    free_append_buffer(&ab);
}


void usage()
{
    fprintf(stdout, "Uso: %s <Arquivo-Saída.t[gbx]z> <Diretório-Alvo>\n",
           PROGNAME);
    return;
}


/* Cuida de inicializar Append_Buffer */
void init_append_buffer(Append_Buffer *ab)
{
    ab->buffer = NULL;
    ab->size = 0;
}


/* Cuida de liberar memória alocada para Append_Bufer */
void free_append_buffer(Append_Buffer *ab)
{
    if (ab->buffer != NULL)
    {
        free(ab->buffer);
        ab->buffer = NULL;
    }
}


/* Preenche Append_Buffer, com os items para fazer o backup */
bool fill_append_buffer(Append_Buffer *ab, char *pathname)
{
    size_t pathname_len = strlen(pathname);
    ab->buffer = realloc(ab->buffer, ab->size + pathname_len + 1);
    if (ab->buffer == NULL)
    {
        fprintf(stderr, "Erro ao alocar memória para lista"
            " de items para backup\n");
        return(false);
    }

    strncpy(ab->buffer + ab->size, pathname, pathname_len);
    ab->size += pathname_len;
    ab->buffer[ab->size] = '\0';
    return(true);
}


/* Cuida de fato de fazer o arquivamento e compactação dos itens */
void write_archive(Append_Buffer *ab, char *outname, int extension)
{
    if (ab->buffer != NULL)
    {
        struct archive *arch;
        struct stat filestat;

        /* Cria um arquivamento, configura compressão e o formato (tar) */
        arch = archive_write_new();
        archive_write_add_filter(arch, extension);
        archive_write_set_format_pax_restricted(arch);
        archive_write_open_filename(arch, outname);

        char *match = strtok(ab->buffer, "#");
        while (match)
        {
            struct archive_entry *entry;
            stat(match, &filestat);

            /* Cria a entrada de arquivamento, com o caminho do item,
             * tamanho, se é diretorio ou arquivo e permissões */
            entry = archive_entry_new();
            archive_entry_set_pathname(entry, match);
            archive_entry_set_size(entry, filestat.st_size);
            if (S_ISDIR(filestat.st_mode))
            {
                archive_entry_set_filetype(entry, AE_IFDIR);
                archive_entry_set_perm(entry, 0755);
            }
            else if (S_ISREG(filestat.st_mode))
            {
                archive_entry_set_filetype(entry, AE_IFREG);
                archive_entry_set_perm(entry, 0644);
            }

            /* Escreve o cabeçalho do item no arquivamento */
            archive_write_header(arch, entry);

            char buff[8192];
            int fd = open(match, O_RDONLY);
            int len = read(fd, buff, sizeof(buff));

            while (len > 0)
            {
                /* Escreve o item no arquivamento */
                archive_write_data(arch, buff, sizeof(buff));
                len = read(fd, buff, sizeof(buff));
            }

            close(fd);
            archive_entry_clear(entry);
            archive_entry_free(entry);

            match = strtok(NULL, "#");
        }

        archive_write_close(arch);
        archive_write_free(arch);
    }
}


/* Cuida de varrer o diretorio alvo, e envia os caminhos
 * para Append_Buffer, para depois então fazer o arquivamento */
void sweep_dir(Append_Buffer *ab, char *dirname)
{
    DIR *dhandle = opendir(dirname);
    if (dhandle == NULL)
    {
        fprintf(stderr, "Erro ao abrir diretório %s\n", dirname);
        exit(EXIT_FAILURE);
    }

    struct dirent *file;
    struct stat filestat;

    while ((file = readdir(dhandle)) != NULL)
    {
        char *entry_path;
        if ((entry_path = create_pathname(dirname, file->d_name)) == NULL)
        {
            fprintf(stderr, "Erro ao alocar memória para entrada %s\n",
                file->d_name);
            closedir(dhandle);
            exit(EXIT_FAILURE);
        }

        stat(entry_path, &filestat);
        if (S_ISDIR(filestat.st_mode))
        {
            /* Aqui garante que não enviamos os diretorios com
             * . e .. */
            if (rev_strstr(entry_path, "/.", 2) == NULL
                && rev_strstr(entry_path, "/..", 3) == NULL)
            {
                /* Escrevemos duas vezes para Append_Buffer, uma
                 * com o caminho e outra com o token para strtok() */
                if (fill_append_buffer(ab, entry_path) == false ||
                    fill_append_buffer(ab, "#") == false)
                {
                    free(entry_path);
                    closedir(dhandle);
                    exit(EXIT_FAILURE);
                }

                /* É diretorio, então faz chamada recursiva para pegar
                 * o conteudo */
                sweep_dir(ab, entry_path);
            }
        }
        else if (S_ISREG(filestat.st_mode))
        {
            if (fill_append_buffer(ab, entry_path) == false ||
                fill_append_buffer(ab, "#") == false)
            {
                free(entry_path);
                closedir(dhandle);
                exit(EXIT_FAILURE);
            }
        }

        free(entry_path);
    }

    closedir(dhandle);
    return;
}


/********************************* entrada ************************************/


int main(int argc, char **argv)
{
    /* Prorama é para ser chamado:
     * ./bmu arquivamento.txz diretorio */
    if (argc < 3)
    {
        usage();
        return(0);
    }

    if (atexit(cleanup) != 0)
    {
        fprintf(stderr, "Erro ao registrar função de saída.\n");
        return(1);
    }

    init_append_buffer(&ab);

    char *outname;
    char *dirname;

    /* Uso a função archive_write_add_filter().
     * Esta função pede o código do compactador
     * não o nome */
    int extension = -1;

    argv++;
    outname = *argv++;
    dirname = *argv;

    /* Já sai se diretorio não existir */
    if (is_valid_directory(dirname) == false)
    {
        fprintf(stderr, "Erro. Diretório especificado inválido\n");
        return(1);
    }

    remove_last_char(dirname, '/');

    /* Descobre a extensão, xz, bz ou gz ou dá erro
     * se não tiver extensão */
    char *ext;
    if ((ext = has_extension(outname)) == NULL)
    {
        fprintf(stderr, "Erro. <Arquivo-Saída> não tem extensão\n");
        usage();
        return(1);
    }

    /* Valida se extensão é txz por exemplo e não
     * algo como bxz txp etc.. */
    if (fnmatch("txz", ext, 0) == 0 || fnmatch("tbz", ext, 0) == 0 ||
        fnmatch("tgz", ext, 0) == 0)
    {
        ext++;
        switch (*ext)
        {
            case 'x':
                extension = XZ;
                break;
            case 'b':
                extension = BZIP;
                break;
            case 'g':
                extension = GZIP;
                break;
        }
    }
    else
    {
        --ext;
        fprintf(stderr, "Erro. Extensão %s inválida ou"
            " não suportada\n", ext);
        usage();
        return(1);
    }


    sweep_dir(&ab, dirname);
    write_archive(&ab, outname, extension);
    free_append_buffer(&ab);
    return(0);
}


/******************************************************************************/
