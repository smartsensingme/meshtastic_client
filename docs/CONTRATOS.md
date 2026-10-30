# Contratos e mapa da implementação

Revisão segundo o perfil completo em 30/10/2026. Este documento complementa os
headers públicos e o README; não muda o protocolo ou a API. Código de terceiros
em generated/reference/nanopb mantém seus avisos e formatação originais.

## Eventos e duração dos dados

Todos os callbacks acontecem na thread dona, nunca na ISR. mc_init/mc_refresh
podem emitir MC_REFRESHING antes de retornar; RX e conclusões TX chegam por poll.
Registro, nomes e payload são emprestados apenas durante a chamada. Copiar antes
de publicar em outra thread. Os campos não aplicáveis começam zerados/NULL.
Consultas de leitura pelo callback da thread dona são permitidas; não reentrar
em init/poll/refresh/send. Callback lento compete com o ring RX limitado.

| Evento | Campos e interpretação |
|---|---|
| SYNCED | node local; staging completo foi publicado, não política de identidade/canal |
| DISCONNECTED | Sessão antes pronta foi invalidada; cache histórico pode permanecer |
| NODE | node, name, short_name; anúncio não autenticado, truncamentos do cache possíveis |
| QUEUE | packet_id, status res do rádio, queue_free/queue_max em slots |
| ROUTE | packet_id=request_id, node origem, destination, status enum Routing.Error |
| RECEIVED | node/destination/id/port/channel/pki_encrypted/payload/length |
| ERROR | status errno negativo; nem todo erro invalida sessão |
| CHANNEL | channel, status enum Channel.Role, name; índice bruto anunciado em evento avulso |
| METADATA | name com versão anunciada |
| REFRESHING | Novo pedido aceito; sessão saudável anterior não é apagada |
| SNAPSHOT_BEGIN | Replay do novo commit: metadata, canais, nós e por fim SYNCED |
| TX_DRAINED | packet_id, status 0/errno negativo, length bytes FIFO incluindo header |

QueueStatus pode recusar um envio que a UART aceitou. Routing ACK não equivale
a processamento remoto. TX_DRAINED não prova que os bits acabaram no fio.
Em cancelamento, bytes já aceitos podem ter chegado ao destino: entrega incerta.
MC_ROUTE mantém payload/length do Routing decodificado, mas sua correlação usa
request_id. NODEINFO e outros pacotes não Routing também geram RECEIVED.

## Fluxo e chamadores

| Implementação | Chamadores internos e trabalho principal |
|---|---|
| event | fault/disconnect/reap_tx/mc_refresh/node_event/commit_snapshot/process; callback opcional |
| fault | process/mc_poll; contador uint32 com wrap e último errno, sem desconexão implícita |
| disconnect | recover_transport/commit_snapshot; invalida synced e emite uma perda |
| irq | driver registrado em mc_init; ring RX/erros e mc_tx_irq, wake do dono |
| write_message | start_snapshot/mc_send/mc_poll; nanopb em tx privado e cópia para fila |
| reap_tx | recover_transport/mc_poll; no máximo depth resultados, id 0 não vira evento público |
| start_snapshot | mc_refresh/mc_poll; nonce, admissão antes de staging e prazos |
| recover_transport | process/mc_poll; disconnect, abort, reap, padding/backoff |
| node_event | commit_snapshot/process; nomes emprestados na chamada de event |
| snapshot_complete | process; checa dados mínimos da geração correlacionada |
| commit_snapshot | process; cópia de staging, retenção de erros e replay |
| process | mc_poll; FromRadio, cache e despacho, não telemetria de aplicação |
| copy_text | node_update/mc_diag_observe/mc_diag_user; truncamento com NUL |
| node_update | mc_diag_observe; atualizar/inserir sem expulsão |
| mc_diag_reset | Testes de cache; não é inicializador público do cliente |
| mc_diag_observe | process; observações permitidas, sem credenciais |
| mc_diag_user | process; NODEINFO substitui User, preserva metadados anteriores |
| add (TX) | put/write/irq/abort; soma saturante sob lock |
| put | mc_tx_write; cópia de slot antes de publicar count |
| mc_tx_init | mc_init; UART quiescente, reinicia ownership/contadores |
| mc_tx_write | write_message; admissão atômica de frame ou padding+frame |
| mc_tx_irq | irq; um serviço FIFO, wake em conclusão/erro |
| mc_tx_take | reap_tx; copia resultado e libera slot |
| mc_tx_abort | recover_transport; retém resultados explícitos dos incompletos |
| mc_tx_error | mc_poll; erro latente ou falta de progresso |
| mc_tx_deadline | mc_wait; próximo timeout, sem timer próprio |
| mc_tx_snapshot | mc_init/mc_snapshot_read; cópia coerente sob lock |

APIs públicas mc_init/poll/wait/send/ready/local_node/snapshot_read não têm
chamadores internos, salvo mc_refresh e mc_wake conforme headers. mc_frame_byte
é pública e também chamada por mc_poll; instâncias independentes não compartilham
estado. Helpers internos exigem ponteiros válidos e não são uma API para consumidores.

## Invariantes e custo

Um produtor (dono) e uma ISR UART. Ring é SPSC; estado de protocolo/Protobuf é
privado do dono. Fila TX e estatísticas usam spinlock por trechos limitados;
nenhum lock aguarda a linha serial ou USB. Ativar TX ocorre fora do lock porque
o driver pode envolver runtime-PM. A memória do payload externo é liberável
imediatamente após send retornar: fila detém sua própria cópia.

`count` inclui pendentes e concluídos não reapados; `done` é o prefixo concluído;
`head` é o primeiro slot não reapado. ISR nunca libera slot. Abort preserva
conclusões já completas e transforma apenas incompletos em erro. Um lote de
recuperação ocupa dois slots ou nenhum. Não existe resultado perdido por falta
de uma segunda fila de conclusões ilimitada.

Cada poll pode drenar RX e emitir muitos callbacks; não se promete WCET fixo.
Snapshot copia tamanho limitado por Kconfig. RX counters são observações
individuais da ISR, TX uma cópia sob lock, sem transação global entre ambos.
Não chamar o cliente de duas threads ou suspender a UART sem resolver o contrato
de recepção/tempo. Espera por semáforo permite idle da CPU, não deep sleep do rádio.

## Compatibilidade e manutenção

Símbolos existentes preservados. mc_diag_log foi removido anteriormente;
diagnostics.h é somente compatibilidade de tipos. Identidade, configuração
persistente, GPIO, bootloader e logs periódicos pertencem ao consumidor.
Atualizar Protobuf/nanopb exige revisão de fontes/licenças, testes wire oficiais,
framing/cache/TX/estado e consumidores; não regenerar um subset de schemas como
se fosse o conjunto completo. Falhas físicas ainda pendentes não são aprovadas
por teste host ou build em outro microcontrolador.
