# Socket no modo não bloqueante

É necessário para implementar a lógica de timeout.
Significa que as funções de rede não ficam "paradas" esperando alguma coisa acontecer, então o programa continua executando imediatamente e as funções retornam na hora.

Exemplo de bloqueante:

`recv(sockfd, buffer, 1000, 0);`

O programa fica travado esperando chegar algo.

Como fica o socket non-blocking:

```
#include <fcntl.h>
fcntl(sockfd, F_SETFL, O_NONBLOCK);
recv(sockfd, buffer, 1000, 0);
```

No modo não bloqueante, o timeout é controlado pelo select() em recv_with_timeout().
fcntl() é uma função Linux/ Unix usada para configurar ou controlar um descritor de arquivo (file descriptor).
O file descriptor é um número inteiro que significa algum recurso aberto no kernel.

"Tem mensagem?"
"Não."
"Ok, continuo trabalhando."
---

FD_SET -> "observe essa caixa"
select -> "me avise quando tiver carta"
recv -> "pegue a carta"

---
# Dúvidas

1. Em que momento nós mudamos a diferença entre o timeout no socket e na aplicação diretamente?