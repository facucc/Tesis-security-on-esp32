#define KEY_SIZE 2048
#define EXPONENT 65537
#define PRIVATE_KEY_BUFFER_SIZE 1700
#define CSR_BUFFER_SIZE 1150

#define DFL_SUBJECT_NAME "CN=AWS IoT Device, O=UNC , OU=Seguridad, C=AR, ST=Cordoba, L=Cordoba"

bool GenerateCSR(char * key_pem, char * csr_pem);