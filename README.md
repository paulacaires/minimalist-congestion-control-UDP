# Explicando as decisões

## Em relação ao pacote
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
