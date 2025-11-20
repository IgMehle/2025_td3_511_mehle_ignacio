/*
version 4.2
thread para leer el buffer de datos y el menu funciona en un thread aparte
con memoria de usuario maestro
log de datos con estampa de tiempo
log de datos actualizado



			
				funciona	
			





*/






#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>  // Para usleep


#define buffersize 128

#define tiempodeespera 10

#define logconsulta "00,l,x,xxxxxxxx,2,00,$"
#define logpedido "00,l,x,xxxxxxxx,0,00,$"




pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;


typedef enum{
	READ,
	WRITE,
	SCANN,
	CONT_DIG_OK,
	CONT_DIG_ERR,
	ACC_OK,
	ACC_DEN,
	ACC_ERROR,
	ACC_MAESTRA,
	ACC_30SEG,
	ACC_SIN_ASIGNAR,
	ACC_ASIGNADO,
	ACC_MASTER,
	CAMBIO_ESTADO
}accion_t;

typedef enum{
	ALARMA,
	PUERTA,
	CIERRE
}tipo_t;



// Prototipos de funciones
void file_write(FILE *file, char *path, char *str);
void file_read(FILE *file, char *path, char *str);

void *proseso(void *parameter);//hilo de lectura de log
void *menu(void *parameter);//hilo de menu



void delay_ms(int milliseconds) {
    usleep(milliseconds * 1000);  // Convierte milisegundos a microsegundos
}


int requestAcc(char *user){
	
	FILE *file;
	char buff[buffersize];
	//char user[3];	
	char pass[10];

	//memset(user, '\0', strlen(user));
	memset(pass, '\0', strlen(pass));
	
	/*
	printf("ingrese numero de usuario: ");
	fgets(user, 3, stdin);
	user[sizeof(user) - 2] = '\0';
	*/
	//strcpy(user,"1");
	//printf("\nIngrese contraseña para ususario %s:",user);
	printf("contraseña:");
	fgets(pass, 10, stdin);
	pass[strlen(pass) - 1] = '\0';
	

	//------invertir contraseña---------
    int longitud = strlen(pass);  // Obtener la longitud de la cadena
    int inicio = 0;                 // Índice inicial
    int fin = longitud - 1;         // Índice final

    
    while (inicio < fin) {
        char temp = pass[inicio];
        pass[inicio] = pass[fin];
        pass[fin] = temp;
        inicio++;
        fin--;
    }
	//------invertir contraseña---------*/



	
	/*
	if(strlen(pass)!=8 || strlen(pass)!=4){//chequeo que sea una contraseña de 4 digitos o la master
		printf("contraseña invalida\n");
		return ACC_DEN;
	}*/
		
							
	
	memset(buff, '\0', strlen(buff));
	strcat(buff, "00,p,");
	strcat(buff, user);
	if(strlen(pass)<8){strcat(buff, ",0000");}
	else{strcat(buff, ",");}
	
	strcat(buff, pass);
	strcat(buff, ",0,00,$");

	printf("%s",buff);
	file_write(file, "/proc/td3/i2c", buff);
				
	delay_ms(10);
						
	file_read(file, "/proc/td3/i2c", buff);
	printf("%s",buff);
							
	char p = buff[19];
						
	printf("%c", p);
	int a = p - '0';
	
	system("clear");
							
	if(a == ACC_OK){
		printf("\nACCESO CONCEDIDO\n");
		return ACC_OK;
	}
							
	if(a != ACC_OK){
		
		printf("\nACCESO DENEGADO\n");
		return ACC_DEN;
		
	}


 }

void *proseso(void *parameter) {
	char aux[buffersize];
	char dato[buffersize+21];
	char id[3];
	char tipo[3];
	char user[3];
	char accion[3];
	char p, q, a, t, u;
	int espera = tiempodeespera;
	int cantidad = 0;
	FILE *archivo;
	FILE *driver;
	

	//-----estampa de tiempo---------
    time_t timestamp;
    struct tm *tiempo_local;
    char estampa[80];
	//-----fin de estampa de tiempo
    
    timestamp = time(NULL);
    tiempo_local = localtime(&timestamp);
    strftime(estampa, sizeof(estampa), "%Y-%m-%d %H:%M:%S", tiempo_local);

    //printf("Fecha y hora formateada: %s\n", estampa);
	
    

	
	

	while (1) {
		
		memset(id, '\0', strlen(id));
		memset(tipo, '\0', strlen(id));
		memset(user, '\0', strlen(id));
		memset(accion, '\0', strlen(id));
		
		file_write(driver, "/proc/td3/i2c", logconsulta);
		delay_ms(1);
		file_read(driver, "/proc/td3/i2c", aux);
		
		p = aux[3];
		
		if(p=='l'){
			q = aux[16];
			if (q == '5') {//ACC_OK significa que hay datos para leer
				
				memset(dato, '\0', strlen(dato));
				//printf("solicitar info\n");
				file_write(driver, "/proc/td3/i2c", logpedido);
				delay_ms(1);
				file_read(driver, "/proc/td3/i2c", aux);
				
				/*
				timestamp = time(NULL);
				tiempo_local = localtime(&timestamp);
				strftime(estampa, sizeof(estampa), "%Y-%m-%d %H:%M:%S", tiempo_local);
				*/
				
				strncpy(id, aux, 2);
				t=aux[3];
				tipo[0]=t;
				strcat(dato, id);
				
				strcat(dato, " ");
				
				timestamp = time(NULL);
				tiempo_local = localtime(&timestamp);
				strftime(estampa, sizeof(estampa), "%Y-%m-%d %H:%M:%S", tiempo_local);
				
				strcat(dato, estampa);
				
				strcat(dato, " ");
				
				if(t=='r'){
					a=aux[19];
					//accion[0]=a;
					if(a=='5'){//ACC_OK
						strcat(dato, "acceso OK usuario: ");
						
					}
					if(a=='6'){//ACC_DEN
						strcat(dato, "intento de acceo fallido usuario: ");
					}
					/*
					if(a=='8'){//ACC_MAESTRO
						strcat(dato, "acceso MAESTRO OK ");
					}
					*/
					if(a=='7'){//ACC_ERROR
						strcat(dato, "intento de acceo MAESTRO fallido ");
					}
					
					u=aux[5];
					
					if(u=='0'){
						strcat(dato, "maestro");
					}
					else{
						user[0]=u;
						strcat(dato, user);
					}
					
				}
				if(t=='a'){
					a=aux[19];
					//accion[0]=a;
					if(a=='5'){
						strcat(dato, "puerta desbloqueada");
						
					}
					if(a=='2'){
						strcat(dato, "puerta desbloqueada MAESTRO");
						
					}
					if(a=='6'){
						strcat(dato, "ALARMA");
					}
				}
				if(t=='p'){
					a=aux[19];
					//accion[0]=a;
					strcat(dato, "Solicitud de acceso por panel, usuario: ");
					u=aux[5];
					
					if(u=='0'){
						strcat(dato, "maestro");
					}
					else{
						user[0]=u;
						strcat(dato, user);
					}
				}
				if(t=='d'){
					a=aux[19];
					//accion[0]=a;
					strcat(dato, "Puerta abierta");
					
					/*
					u=aux[5];
					
					if(u=='0'){
						strcat(dato, "maestro");
					}
					else{
						user[0]=u;
						strcat(dato, user);
					}*/
				}
				strcat(dato, "\n");
				
				
				
				//strcat(dato, estampa);
				//strcat(dato, aux);
				
				
				
				pthread_mutex_lock(&mutex);
				archivo = fopen("/home/german/Desktop/archivo.txt", "a");
				if (archivo == NULL) {// Verificar si el archivo se abrió correctamente
					printf("TD3 - No se pudo crear el archivo.\n");
					return 1;  // Terminar el programa con un error
				}
				else{
					fprintf(archivo, dato);
					fprintf(archivo, "\n");
					fclose(archivo);
				}
				//chequear si hay mas informacion
				
				pthread_mutex_unlock(&mutex);
				espera = 0;
				
			} else if (q == '6') {//ACC_ER no hay datos para leer
				espera = 10;
				//printf("Se ha cargado el valor: %d\n", valor);
			}
		}	
		else{
			espera = tiempodeespera;
		}
		
		//chequqear si hay info con "espera"
		
		
		//printf("hilo");
		sleep(espera);
		//pthread_cond_timedwait(&cond, &mutex, &delay_time);
		
		
		
		
		
	}
	
}

void *menu(void *parameter) {
	 // Puntero a archivo
	FILE *file;
	FILE *datos;
	int aux=0;
	int auxmenu=0;
	int auxlog=0;
	int acceso = ACC_DEN;
	int master,tempmaster = ACC_DEN;
	char str[buffersize];
	
	char straux[buffersize];	
	char user[3];	
	char pass[10];
	char op[3];
	
	memset(str, '\0', strlen(str));
	
	system("clear");
	

	printf("Bienvenido al sistema de control de acceso \n");
	//sleep(6);
	
	
	while(1) {
        //bucle para esperar confirmacion de acseso
		while(acceso != ACC_OK){
			
					
			printf("\n Ingrese numero de usuario: ");
			fgets(user, 3, stdin);
			user[sizeof(user) - 2] = '\0';
			
			acceso = requestAcc(user);
		
		}
		
		if(user[0] == '0'){
			master = ACC_OK;
			printf("ACCESO MAESTRO\n");
		}
		//acceso = ACC_DEN;

		memset(user, '\0', strlen(user));

		printf("  1 .Ingresar con usuario\n  2 .Menu\n  3 .Log\n  exit .salir del programa\nIngrese una opcion:");
		fgets(straux, 100, stdin);

		if(!strcmp(str, "exit\n")) {
			puts("Saliendo del programa...");
			return 0;
		}
		aux = atoi(straux);

		switch(aux){
			//---------1.ingresar con usuario-------------------------------------
			case 1: 
				
				printf("Ingrese numero de usuario: ");
				fgets(user, 3, stdin);
				user[sizeof(user) - 2] = '\0';
				
				acceso = requestAcc(user);
				
				if(acceso == ACC_OK){
					
					printf("\nABRIENDO PUERTA \n");
					//operar("5");//5=accseso OK
					
					memset(str, '\0', strlen(str));
					strcpy(str, "00,a,1,5,$");
					
					//file_write(file, "/proc/td3/i2c", str);
								
				}
				memset(user, '\0', strlen(user));
				break;
				
			//------------2.menu----------------
			case 2: 
				//agregar verificacion de usuario maestro
				
				if(master!=ACC_OK){
					printf("Se requiere contraseña maestra\n");
					tempmaster = requestAcc("0");
				}
				if(master==ACC_OK || tempmaster==ACC_OK){
					tempmaster = ACC_DEN;
					printf("  1 .Asignar usuario\n  2 .borrar usuario\n  3. menu anterior \nIngrese una opcion: ");
				
					fgets(straux, 100, stdin);
					auxmenu = atoi(straux);
					
					system("clear");
					
					switch(auxmenu){
						
						//--------------2.menu 1.asignar usuario------------------------
						case 1:
							memset(user, '\0', strlen(user));
							memset(pass, '\0', strlen(pass));
							
							printf("ingrese numero de usuario a asignar: ");
							fgets(user, 3, stdin);
							user[sizeof(user) - 2] = '\0';
							
							memset(str, '\0', strlen(user));
							strcat(str, "00,p,");
							strcat(str, user);
							
							/*codigo para consulta si el ususario ya esta asignado
							strcat(str, ",0000,0,2,$");//2=scan
							
							file_write(file, "/proc/td3/i2c", str);
							
							sleep(1);
							
							file_read(file, "/proc/td3/i2c", str);
							
							char p = str[14];
							int a = p - '0';
							
							if(a == ACC_ASIGNADO){
								ACC_ASIGNADO;
							}
							else{
								ACC_SIN_ASIGNAR;
							}
							
							if(requestNuevo(user) != ACC_ASIGNADO){
								printf("\nNo se uede agregar usuario\n");
							}
							else{*/
								printf("\nIngrese contraseña:");
								fgets(pass, 10, stdin);
								pass[strlen(pass) - 1] = '\0';
								
								
							memset(str, '\0', strlen(user));
							strcat(str, "00,p,");
							strcat(str, user);
							strcat(str, ",0000");
							
							//---invertir caracteres
							int longitud = strlen(pass);  // Obtener la longitud de la cadena
							int inicio = 0;                 // Índice inicial
							int fin = longitud - 1;         // Índice final

							
							while (inicio < fin) {
								char temp = pass[inicio];
								pass[inicio] = pass[fin];
								pass[fin] = temp;
								inicio++;
								fin--;
							}
							//----invertir caracteres
							
							
							
							strcat(str, pass);
							strcat(str, ",0,01,$");//1=write
							
							
							system("clear");
							
							file_write(file, "/proc/td3/i2c", str);
							printf("USUARIO AÑADIDO\n\n");
							memset(user, '\0', strlen(user));
							//}    del else
							break;
						//---------------2.menu .2borrar usuario---------------------------------------
						case 2:
						
							memset(user, '\0', strlen(user));
							memset(pass, '\0', strlen(pass));
							
							printf("ingrese numero de usuario a borrar: ");
							fgets(user, 3, stdin);
							if(user[0]=='0'){
								printf("No se puede eliminar el usuario maestro\n");
								break;
							}
							user[sizeof(user) - 2] = '\0';
							strcpy(pass, "0000");
							
							memset(str, '\0', strlen(str));
							strcat(str, "00,p,");
							strcat(str, user);
							strcat(str, ",");
							strcat(str, pass);
							strcat(str, ",0,01,$");//1=write
							
							
							system("clear");
							
							file_write(file, "/proc/td3/i2c", str);
							printf("USUARIO BORRADO\n\n");
							memset(user, '\0', strlen(user));
							break;
						case 3:
							break;
						default:
							break;
					}
				}
				else{
					printf("No es ususario maestro\n");
				}
			break;
			//-------------3.log---------------------------------
			case 3:
			
				if(master!=ACC_OK){
					printf("Se requiere contraseña maestra\n");
					tempmaster = requestAcc("0");
				}
				if(master==ACC_OK || tempmaster==ACC_OK){
					tempmaster = ACC_DEN;
					printf("  1 .ver log\n  2 .borrar log\n  3. menu anterior \nIngrese una opcion: \n");
					
					fgets(straux, 100, stdin);
					auxlog = atoi(straux);
					
					system("clear");
						
					switch(auxlog){
						case 1:
							pthread_mutex_lock(&mutex);
							datos = fopen("/home/german/Desktop/archivo.txt", "r");
							if (datos == NULL) {
								printf("TD3 - Error al abrir el archivo para lectura.\n");
								return 1;
							}
						
							memset(str, '\0', strlen(str));
							
							printf("ultimos eventos registrados:\n");
							while (fgets(str, sizeof(str), datos) != NULL) {
								printf("%s", str); 
							}
							//muestra la totalidad el archivo hasta encontrar el caracter nulo
							fclose(datos);
							pthread_mutex_unlock(&mutex);
							
							
							//printf("log de eventos no disponible");
							break;
						case 2:
							pthread_mutex_lock(&mutex);

							datos = fopen("/home/german/Desktop/archivo.txt", "w");
							if (datos == NULL) {
								perror("Error al abrir el archivo");
								return 1;
							}
							printf("Contenido del log borrado.\n");
							fclose(datos);

							pthread_mutex_unlock(&mutex);
							break;
					}		
				}
				break;	
			case 4:
				exit(0);
			default:
				system("clear");
				printf("opcion invalida\n");
				break;
		}
	}
	
}


/**
 * @brief Programa principal
 */
int main(int argc, char *argv[]) {
	
	pthread_t id_proseso;
	pthread_t id_menu;
	if (pthread_create(&id_proseso, NULL, proseso, NULL) != 0) {
			perror("TD3 - Error al crear el thread proseso");
			exit(1);
	}
	if (pthread_create(&id_menu, NULL, menu, NULL) != 0) {
			perror("TD3 - Error al crear el thread proseso");
			exit(1);
	}
	
	
	//pthread_detach(id_proseso);  // Separar el hilo para liberar recursos
	//pthread_detach(id_menu);  
	pthread_join(id_proseso, NULL);
	pthread_join(id_menu, NULL);
	
   
        // Escribo y leo el archivo
		
	//	file_write(file, "/proc/td3/i2c", str);
	//	sleep(1);
	//	file_read(file, "/proc/td3/i2c", str);
        // Muestro lectura por consola
	
}

 
/**
 * @brief Wrapper para escribir un archivo
 * @param file puntero a archivo
 * @param path ruta al archivo
 * @param str cadena de caracteres para escribir
 */
void file_write(FILE *file, char *path, char *str) {
    // Abro archivo con permisos de escritura, escribo y cierro
	//printf("escritura");
	//pthread_mutex_lock(&mutex);
	//printf("escritura mutex");
	file = fopen(path, "w");
	fputs(str, file);
	fclose(file);
	//pthread_mutex_unlock(&mutex);
}

/**
 * @brief Wrapper para leer un archivo
 * @param file puntero a archivo
 * @param path ruta al archivo
 * @param str puntero donde escribir el contenido del archivo
 */
void file_read(FILE *file, char *path, char *str) {
	//printf("lectura");
	//pthread_mutex_lock(&mutex);
	//printf("lectura mutex");
    // Abro archivo con permisos de lectura, leo y cierro
	file = fopen(path, "r");
	fgets(str, 100, file);
	fclose(file);
	//pthread_mutex_unlock(&mutex);
}
