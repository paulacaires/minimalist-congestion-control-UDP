# Como eu executo

- Abro o WSL

- 


# Explicando as decisões

# Em relação ao pacote
No arquivo `packet.h`, nós usamos o tipo de dado uint16_t.
Ele representa "a fixed-width integer type that represents an unsigned 16-bit integer", para evitar o padding ou variações no tamanho dos campos do pacote.

### Por que #pragma pack(push, 1)?
Para que a struct não tenha espaços invisíveis para alinhamento de memória (padding).
Para mais explicações: https://stackoverflow.com/questions/3318410/pragma-pack-effect

### uint8_t flags;
Um único campo com 8 bits e cada bit representa uma flag diferente.

bit 0 → ACK
bit 1 → SYN
bit 2 → FIN
bits 3–7 → não usados (por enquanto)

Como ativar o bit:
packet.flags |= FLAG_ACK;
> O operador |= (OR bit a bit) “Liga o bit do ACK sem mexer nos outros”

Como desativar o bit:
packet.flags &= ~FLAG_ACK;

# Em relação ao servidor.c
Receptor UDP com simulação de perda de pacotes
Passo a passo do fluxo do servidor:
1. Cria socket UDP
2. Three-way handshake (Espera SYN e responder com SYN+ACK)
3. Recebe pacote de dados e envia os ACKs correspondentes
4. Simula a perda de pacotes (de forma random)
5. Se receber FIN -> encerra a comunicação

## #include <arpa/inet.h>
Conversão de endereços e bytes para rede

### File descriptor
> Resumo rápido: identificador de um arquivo/recurso aberto

A função socket() retorna um file descriptor (FD), que é um inteiro que representa um recurso aberto no sistema.
Não retorna o socket em si e sim um handle (referência) para algo dentro do kernel.
É essa referência que você usa em outras funções para se referenciar ao socket.

`int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);`

De acordo com a [documentação](https://pubs.opengroup.org/onlinepubs/009696599/functions/socket.html), esses são os parâmetros para a função socket:

domain | "Onde?"
    Define o domínio. Tipos possíveis e os seus significados:
    *AF_INET*: AF = Address Family, INET = Internet | IPv4
    *AF_INET6*: IPv4
    *AF_UNIX*: Conexão local (arquivo)
type | "Como?"
    Comportamento do socket. Tipos possíveis e os seus significados:
    *SOCK_DGRAM*: Significa que vamos usar o UDP
    *SOCK_STREAM*: Protocolo TCP
    *SOCK_SEQPACKET*
protocol
    Especifica um protocolo específico, como no caso é "protocol = 0", então o socket é default. "Specifying a protocol of 0 causes socket() to use an unspecified default protocol appropriate for the requested socket type."

Retorno negativo significa erro: "Upon successful completion, socket() shall return a non-negative integer, the socket file descriptor. Otherwise, a value of -1 shall be returned and errno set to indicate the error."

### Configuração dos endereços
```
#include <netinet/in.h>

struct sockaddr_in {
    short            sin_family;   // tipo de endereço (IPv4) e.g. AF_INET
    unsigned short   sin_port;     // porta e.g. htons(3490)
    struct in_addr   sin_addr;     // endereço IP
    char             sin_zero[8];  // zero this if you want to
};

struct in_addr {
    unsigned long s_addr;  // load with inet_aton()
};
```

Essa struct é definida na biblioteca "#include <netinet/in.h>"
Structures for handling internet addresses.

Precisa da função htons(8080) para definir uma porta:
Host TO Network Short
Converte o número para o formato da rede (big-endian), relacionado a como os números são guardados na memória, existem duas formas (Endianness (ordem dos bytes)):
1. Big-endian (formato da rede) | O número mais significativo à esquerda
        0x1234 → [0x12][0x34]
                    ↑ mais significativo primeiro
2. Little-endian (PC comum) | "Invertido"
        0x1234 → [0x34][0x12]
                    ↑ menos significativo primeiro
A maioria dos computadores utiliza o segundo caso (little-endian), por isso é necessário usar a função htons().

#### memset(&server_addr, 0, sizeof(server_addr));
Função que preenche um bloco de memória com um valor, preenchendo toda a struct server_addr com zeros para não manter lixo de memória.

#### Bind
ANTES do bind:
socket existe, mas não tem endereço

DEPOIS do bind:
socket está “escutando” em 0.0.0.0:8080

Em relação ao endereço do cliente (client_addr), quem define é o sistema e não eu. Eu saberei o endereço do cliente depois da função 

`recvfrom(sockfd, ..., (struct sockaddr *)&client_addr, &client_len);`

"The recvfrom() function is a standard system call used to receive data from a socket. It is primary for connectionless protocols (like UDP) because it captures the sender's address along with the received data."

```
#include <sys/socket.h>

ssize_t recvfrom(int socket, void *restrict buffer, size_t length,
                 int flags, struct sockaddr *restrict address,
                 socklen_t *restrict address_len);
```
receive a message from a socket

`ssize_t` é um tipo de dados inteiro com um significado: tamanho ou erro.
- Por isso, se o retorno da função recfrom for maior que zero, então significa o número de bytes que foram recebidos. Caso for um valor negativo (-1 etc) então significa que houve um erro. 

`socklen_t client_len = sizeof(client_addr);`
socklen_t é um tipo de dado para armazenar o tamanho de estruturas de endereço (typedef para portabilidade entre sistemas, em vez de ser um "int" truncado por exemplo).

---

int opt = 1;
setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

Para resolver o erro: "bind: Address already in use"

# ISN_cliente e ISN_servidor
Cada lado da conexão escolhe seu próprio número inicial, independente

# inet_pton
Converte um endereço IP em formado de texto (string) para binário, que o socket usa.