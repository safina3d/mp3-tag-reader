#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "tag.h"


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Methodes TP1 /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Permet de lire un octet */
int read_u8(int fd, u8 *val){
	if( read(fd, val, sizeof(u8)) < 0 ){
		perror("--> Erreur lecture u8!.");
		return 0;
	}
	return 1;	
}

/** Permet de lire deux octets */
int read_u16(int fd, u16 *val){
	if ( read(fd,val, sizeof(u16) ) == -1 ){
	  perror("--> Erreur lecture u16!.");
	  return 0;
	}
	//Conversion BigEndian en littleEndian
	*val = (*val & 0xFF00) >> 8 | (*val & 0x00FF) << 8;
	return 1;
}

/** Permet de lire Quatre octets */
int read_u32(int fd, u32 *val){
	if ( read(fd,val, sizeof(u32) ) == -1 ){
	  perror("--> Erreur lecture u16!.");
	  return 0;
	}
	//Conversion BigEndian en littleEndian
	*val =	(*val & 0xFF000000) >> 24 | (*val & 0x00FF0000) >> 8  |	(*val & 0x0000FF00) << 8  |	(*val & 0x000000FF) << 24 ;
	return 1;	
}

/** Permet de convertir un entier de quatre octets (littleEendian) en un autre dont le bit 7 de chaque octet aura été ignoré */
u32 convert_size(u32 size){
	return ( (size & 0xFF000000) >> 3  | (size & 0x00FF0000) >> 2  | (size & 0x0000FF00) >> 1  | (size & 0x000000FF) );	
}

/** Permet de lire size octets depuis le fichier fd selon l’encodage encoding et de stocker le resultat dans la chaine "to" */
char *read_string(int fd, char *to, int size, int encoding){
	int i = 0; 
	char c;	
	u16 enc;

	if(to == NULL){	to = (char *) malloc( size+1 * sizeof(char) ); }
	
	// avancer la tete de lecture de 2 octets si l'encodage est utf8
	if(encoding != 0){	
		read_u16(fd, &enc); 
		// printf("ENCODAGE = %2x\n", enc);		// FFFE ou FEFF 
	}
	// Lecture de la chaine et stockage dans "to"
	while(i < size){
		if(encoding == 0) {
			read(fd, &c, 1);
			to[i]=c; i++;
		}else{
			if(enc == 0xFEFF){
				lseek(fd, 1, SEEK_CUR);
				read(fd, &c, 1);
				to[i] = c;
			}else{
				read(fd, &c, 1);
				to[i] = c;
				lseek(fd, 1, SEEK_CUR);
			}
			i++;
			// cas ou l'encodage est utf8, inserer caractere fin de chaine si i > (size/2-2) (2 oct corresp a FFEE ou FEFF)
			if(encoding != 0 && i>(size/2)-2) {
				to[i] = '\0';
				i = size;
			}
		}
	}
	to[i] = '\0';
	return to;
}

/** Permet de lire et recuperer l'entete du tag id3v2 depuis le fichier fd */
int tag_read_id3_header(int fd, id3v2_header_t *header){
	char *_id = NULL;
	u16   _version;	
	u8    _flags; 	
	u32   _size;

	// lecture de l'ID
	_id = read_string(fd, _id, 3, 0); 		/* "ID3" */

	//Recuperation et Verification des informations stockees dans l'entete
	if( strcmp(_id, "ID3") != 0 || read_u16(fd, &_version) == 0 || read_u8(fd, &_flags) == 0 ||  read_u32(fd, &_size)    == 0 ) 
		return -1;
	
	strcpy(header->ID3, _id);
	header->version = _version; 
    header->flags   = _flags;
    header->size    = convert_size(_size);
    return 0;
}

/** Permet de lire et recuperer les informations stockees dans une frame depuis le fichier fd */
int tag_read_one_frame(int fd, id3v2_frame_t *frame){
	char *_id = NULL;
	u32 _size; 
	u16 _flags;
	u8  _enc;
	char *_txt = NULL;

	_id = read_string(fd, _id, 4, 0);

    if( read_u32(fd, &_size) == 0 || read_u16(fd, &_flags) == 0 ) return -1 ;

    // Gestion des frames invalides 
	if( _id[0] == '\0' || _id[0] != 'T' || strcmp(_id, "TXXX") == 0){
		strcpy(frame->id, _id);
		lseek(fd, _size, SEEK_CUR);  
		return -2; 
	} 

    strcpy(frame->id, _id);
    frame->size  = convert_size(_size); 	
    frame->flags = _flags;
    read_u8(fd, &_enc);     //printf(" {%x} : ", _enc );
    _txt = read_string(fd, _txt, (frame->size)-1, _enc);
    frame->text = _txt;
    return 0;
}

/**Permet d'ouvrir le fichier, lire l’entete et lire toutes les frames contenues dans le fichier */
id3v2_frame_t *tag_get_frames(const char *file, int *frame_array_size){
	
	int fd = open( file, O_RDONLY );
	if ( fd == -1){	
		perror ( file );
		exit (1);
	}

	id3v2_frame_t  *tabFrames = NULL;      // tableau de frames
	id3v2_header_t currentHeader;
	id3v2_frame_t  currentFrame;
	int i = 0;

	tabFrames  = (id3v2_frame_t *) malloc( *frame_array_size * sizeof(id3v2_frame_t) );

	if(tabFrames == NULL){
		printf(">> Erreur lors du malloc: impossible de creer un tableau de frames !!\n");
		free(tabFrames);
		exit(1);
	}
	
	// lecture de l'entete
	tag_read_id3_header(fd, &currentHeader);

	// lecture des frames
	while( tag_read_one_frame(fd, &currentFrame) == 0 ){
		
		// Cas ou il existe encore des frames a lire
		if(i >= *frame_array_size){
			tabFrames = (id3v2_frame_t *) realloc(tabFrames,  ++(*frame_array_size) * sizeof(id3v2_frame_t));
			if(tabFrames == NULL){
				printf(">> Erreur lors du realloc: impossible de changer la taille du tableau tabframes !!\n");
				free(tabFrames);
				exit(1);
			}
		}
		tabFrames[i++] = currentFrame;
	}
	close(fd);
	if(i==0) return NULL; // cas ou le fichier ne contient pas de frames
	return tabFrames;
}

/**  Permet de librer la memoire pointee par frames qui contient frame_count frames */
void tag_free(id3v2_frame_t *frames, int frame_count){
	if(frames != NULL){
		free(frames);
		frames = NULL;
		frame_count = 0;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Methodes TP2 /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Permet de recuperer l'extension d'un fichier */
const char *get_file_extension(const char *file){
	char *ext = strrchr( strrchr(file, '/'), '.' );
	if(ext == NULL) return "";	
	return ext;
}

/** permet de nettoyer une chaine de caractere */
void clean_string(char *s){
	int i=0;
	do{
		if(s[i] == '/') s[i] = '-';
		if(i==0)	s[i] = toupper(s[i]) ;
		else		s[i] = tolower(s[i]) ;
		i++;
	}while( s[i] != '\0');
}

/** Recupere le nomsd'artiste, d'album, le genre, le titre et le numero depuis les frames d'un fichier mp3 et copie le tou dans une structure sort_info_t */  
int get_file_info(const char *source_file, sort_info_t *info){
	int nbFrames = 0, i = 0;
	id3v2_frame_t currentFrame;
	// Initialisation 
	info->nomArtiste = info->nomAlbum = info->genre = info->titrePiste = (char *) "Inconnu";
	info->numPiste = (char *) "0";
	
	id3v2_frame_t  *tabFrames = tag_get_frames(source_file, &nbFrames);

	if( tabFrames == NULL ) return 0;
	
	while(i<nbFrames){
		currentFrame = tabFrames[i];
		//afficherInfosFrame(&currentFrame);
		if( strcmp(currentFrame.id, "TPE1") == 0 ){ info->nomArtiste = currentFrame.text; clean_string(info->nomArtiste); }
		if( strcmp(currentFrame.id, "TALB") == 0 ){ info->nomAlbum   = currentFrame.text; clean_string(info->nomAlbum);   }
		if( strcmp(currentFrame.id, "TCON") == 0 ){ info->genre      = currentFrame.text; clean_string(info->genre);      }
		if( strcmp(currentFrame.id, "TIT2") == 0 ){ info->titrePiste = currentFrame.text; clean_string(info->titrePiste); }	
		if( strcmp(currentFrame.id, "TRCK") == 0 ){ info->numPiste   = currentFrame.text; clean_string(info->numPiste);   }										
		i++;
	}
	return 1;
}

/** Retourne l'arborescence : par Nom Artiste */
const char *get_artist_folder(char *buffer, int size, const char *root_folder, const sort_info_t *info){
	buffer = (char *) malloc( ++size * sizeof(char) );
	snprintf(buffer, size, "%sBy Artist/%c/%s/%s - %s/%c", root_folder, info->nomArtiste[0], info->nomArtiste, info->nomArtiste, info->nomAlbum, '\0' );
	//buffer[strlen(buffer)-1] = '\0';
	return buffer;
}

/** Retourne l'arborescence : par Genre */
const char *get_genre_folder(char *buffer, int size, const char *root_folder, const sort_info_t *info){
	buffer = (char *) malloc( ++size * sizeof(char));
	snprintf(buffer, size, "%sBy Genre/%s/%s/%s - %s/%c", root_folder, info->genre, info->nomArtiste, info->nomArtiste, info->nomAlbum, '\0' ); 
	//buffer[size-1] = '\0';
	return buffer;
}

/** Creer un repertoire a l'emplacement specifie */
int check_and_create_folder(const char *path){
	// Verifier l'existence du dossier
	if(access(path, F_OK)==0){
		// Le dossier existe
		if(access(path, R_OK) != 0 || access(path, W_OK) != 0 || access(path, X_OK) != 0){
			printf("\t --> Erreur: les droits ne sonts pas bons !!: [%s]\n", path);
			//chmod(path, S_IRWXU); //changement des droits
			return -1;
		}
	}else{
		// Creation d'un nouveau dossier
		if( mkdir(path, S_IRWXU)==0 ){
			//printf("\t\t\t --> Creation nouveau dossier: [%s] OK!\n", path);
			return 0;	
		}else{
			printf("\t\t\t --> Erreur lors creation nouveau dossier: |%s|\n", path);
			return -1;
		} 
	}
	return 0;
}

/** Permet de creer toute l’arborescence */
int create_tree(const char *fullpath){
	int i = 0;
	char *rep = (char *) malloc( (strlen(fullpath)+1) * sizeof(char) );  	//chaine qui contiendra le chemin des rep a creer 
	do{
		// Recuperer le nom du dossier a creer
		do{
			rep[i] = fullpath[i];
			i++;
		}while(rep[i-1] !='/' && rep[i-1] != '\0');
		rep[i]='\0';
		// Creation du dossier
		if( check_and_create_folder(rep) != 0 ) return -1;
	}while(fullpath[i] !='\0');
	//free(rep);
	return 0;
}

/** Permet de construire le nom du fichier a generer */
const char *get_file_name(char *buffer, int size, const sort_info_t *info, const char *ext){
	buffer = (char *) malloc( ++size * sizeof(char) );
	snprintf(buffer, size, "%s - %s - %s.%s%s%c", info->nomArtiste, info->nomAlbum, info->numPiste, info->titrePiste, ext, '\0');
	return buffer;	
}

int sort_file(const char *root_folder, const char *source_file){
	// Recuperation des informations	
	sort_info_t info;
	if(get_file_info(source_file, &info) == 1){
		const char *c_Art = get_artist_folder("", 4096, root_folder, &info );
		const char *c_Gen = get_genre_folder( "", 4096, root_folder, &info );	
		const char *nomFichier = get_file_name( "", 4096, &info, get_file_extension(source_file) );
		create_tree(c_Art);
		create_tree(c_Gen);
		// printf("\t\tchemin art |%s|\n", c_Art);
		// printf("\t\tchemin gen |%s|\n", c_Gen);
		// printf("\t\tnomFichier |%s|\n", nomFichier);
		
		//Generer les chemin des liens
		char *c_Lien_Art = malloc( (strlen(c_Art)+strlen(nomFichier)+1) * sizeof(char) );
		strcpy(c_Lien_Art, c_Art); 		
		strcat(c_Lien_Art, nomFichier);			
		
		char *c_Lien_Gen = malloc( (strlen(c_Gen)+strlen(nomFichier)+1) * sizeof(char) );
		strcpy(c_Lien_Gen, c_Gen);
		strcat(c_Lien_Gen, nomFichier);

		if( link(source_file, c_Lien_Art ) == 0 && 	link(source_file, c_Lien_Gen ) == 0 ){
			unlink(source_file);
		}	
		free(c_Lien_Art);
		free(c_Lien_Gen);
		return 0; 
	}

	return -1;

}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Autres methodes //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Permet d'afficher le contenu d'une entete */
void afficherInfosHeader(const id3v2_header_t *h){
	//printf("   %s       %s   %s    %s\n", "ID", "Ver", "Flags", "Size");
	//printf("[ \"%s\" | 0x%04x | 0x%x | 0x%08x ]\n", h->ID3, h->version, h->flags, h->size);
	printf("\n  ID      : \"%s\" \n  Version : 0x%04x \n  Flags   : 0x%x \n  Size    : 0x%08x \t= %d oct\n", h->ID3, h->version, h->flags, h->size, h->size);
}

/** Permet d'afficher le contenu d'une frame */
void afficherInfosFrame(const id3v2_frame_t *f){
	if( (f->id)[0] != 'T' || strcmp(f->id, "TXXX") == 0)
		printf("--- Frame ignoree {%s} ---\n", f->id ); 
	else
		//printf("  [ \"%s\" | 0x%08x | 0x%04x | \"%s\" ]\n", f->id, f->size, f->flags, f->text );
		printf("  [ \"%s\" | %4d oct  | 0x%04x | \"%s\" ]\n", f->id, f->size, f->flags, f->text );
}

/** Permet d'afficher les infos d'une structure sort_info_t */
void afficherFileInfos(const sort_info_t *info){
	printf("\t\t\t╔════════════════════════════════════════════════════════╗\n");
	printf("\t\t\t  %-10s %s\n", "Artiste :", info->nomArtiste );
	printf("\t\t\t  %-10s %s\n", "Album   :", info->nomAlbum );
	printf("\t\t\t  %-10s %s\n", "Genre   :", info->genre );
	printf("\t\t\t  %-10s %s\n", "Titre   :", info->titrePiste );
	printf("\t\t\t  %-10s %s\n", "Numero  :", info->numPiste );
	printf("\t\t\t╚════════════════════════════════════════════════════════╝\n");
}

/** Permet d'afficher la liste des frames contenu dans un tableau de frames */
void afficherLesFrames(const id3v2_frame_t *tabFrames, int nbFrames){
	int i=0;
	if(nbFrames == 0){
		printf("Ce fichier MP3 ne contient pas de frames !\n");
		return;
	}
	while(i<nbFrames){
		afficherInfosFrame( &(tabFrames[i]) );
		i++;
	}
}

void afficherInfosFichierMP3(const char *file){

	int fd = open( file, O_RDONLY );
	if ( fd == -1){	
		perror ( file );
		exit (1);
	}

	id3v2_header_t h;
	id3v2_frame_t f;
	printf("\n► Fichier: %s \n", file);
	printf("╔══ HEADER ══════════════════════════════════════════════════════════════════════╗");
	tag_read_id3_header(fd, &h);
	afficherInfosHeader(&h);
	printf("╠═ FRAMES ═══════════════════════════════════════════════════════════════════════╣");
	int res = 0;
	int sommeFrameSize = 0;
	printf("\n      %s        %s      %s      %s\n", "ID", "Size", "Flags", "Valeur");
	while( res == 0 && sommeFrameSize < h.size ){
		res = tag_read_one_frame(fd, &f); 
		if(res == 0)
			afficherInfosFrame(&f);
		if( res == -2 && strlen(f.id)>0 ) {
			if(AFFICHER_INVALID_FRAMES )	
				printf("    --- Frame ignoree {%s} --- \n", f.id ); 
			res = 0; 
		}
		sommeFrameSize += f.size;
	}
	printf("╚════════════════════════════════════════════════════════════════════════════════╝\n");
	printf("Taille Total = %d , Somme Taille Frames = %d \n", h.size, sommeFrameSize);	

	sort_info_t infos;
	get_file_info(file, &infos);
	afficherFileInfos(&infos);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MAIN  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]){

	if(argc < 3)
		printf("\t Erreur: pas assez d'arguments !!\n\t\t >> musicsort repertoire_destination fichier1 [fichier2 ... fichier3]\n" );
	else{
		int i = 2;
		while(i<argc){
			afficherInfosFichierMP3(argv[i]);
			sort_file( argv[1], argv[i]);
			i++;
		}
		
	} 
	
	return EXIT_SUCCESS;
}