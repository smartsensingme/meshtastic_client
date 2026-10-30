# meshtastic_client

Módulo Zephyr reutilizável em shared/modules/meshtastic_client, com Git local
sem remoto. O consumidor usa link de integração e modules.lock.json. Não depende
de aplicações de telemetria. API em
`include/meshtastic_client/client.h` e `include/meshtastic_client/snapshot.h`. Não contém nomes de nós, JSON, PSKs ou
pinagem de placas. Recebe um dispositivo UART configurado pela aplicação.

Chamar mc_init, mc_poll, mc_wait e mc_send de uma única thread. Callbacks são
síncronos e seus dados só valem durante a chamada. RX usa interrupções e ring
buffer de 4096 bytes. mc_wait aguarda semáforo de RX ou o próximo prazo interno,
limitado pelo prazo de aplicação informado; mc_wake é seguro em contexto de ISR.
TX usa a mesma API UART interrupt-driven, com fila fixa de frames copiados.
Não há uart_poll_out no cliente, espera por espaço ou espera pela linha física.
A aplicação continua dona de init/poll/wait/send; somente a ISR alimenta FIFO.

Framing 0x94/0xc3 + comprimento big endian, limite de 512 bytes; timeout parcial
2 s. Snapshot novo é montado separadamente e publicado apenas após conclusão
correlacionada e identificação local. MC_REFRESHING não desconecta nem invalida
o snapshot anterior. MC_SNAPSHOT_BEGIN precede a reapresentação dos metadados,
canais e nós para a aplicação atualizar suas validações sem perder ACK pendente.
Reboot, perda de liveness e falha UART invalidam a sessão explicitamente.

Heartbeat neutro a cada 60 s (configurável); liveness padrão 180 s. Recuperação
inicia em 20 s e dobra até 300 s: pode continuar indefinidamente nessa frequência
limitada enquanto o rádio estiver ausente. Não há retransmissão LoRa própria.
O rádio cuida de criptografia, roteamento e retransmissão. NODEINFO_APP atualiza
nomes no cache sem solicitar uma configuração completa. Fila expõe free/maxlen;
o acompanhamento de cada envio pertence à aplicação.

O módulo depende de Zephyr, rt_log e da API Meshtastic compatível, não de nRF52, Seeed,
ESP32 ou SX1262 específicos. UART/pinos são fornecidos pelas aplicações. A rotina
nRF de entrada no bootloader está em firmware/support, fora do módulo.

Opções do cliente controlam trace, refresh, heartbeat, liveness, sincronização,
backoff e atributos ACK/hop dos pacotes. Debug, status, descoberta e timeout de
acompanhamento de ACK são políticas em ../support/Kconfig.mesh_policy.
Os padrões preservam o teste com status de 5 s e snapshot de 60 s. operation.conf
nas aplicações reduz logs e desliga refresh periódico; descoberta só solicita
snapshot enquanto falta o destino. Isso não ativa deep sleep no ESP32/rádio.

mc_send proíbe broadcast e destino local nesta primeira versão. ACKs de roteamento
não equivalem a processamento da aplicação remota. O módulo não provisiona rádios.

## Fontes fixadas

- Firmware Meshtastic: tag v2.7.26.54e0d8d.
- Protobufs: 6b1ded439633cd03d4af85b44231b91d1d106278, submódulo da tag.
- generated/meshtastic: cabeçalhos e descritores nanopb oficiais de
  src/mesh/generated/meshtastic da mesma tag; arquivos .pb.cpp armazenados como
  .pb.c (conteúdo inalterado, descritores compiláveis em C).
- Runtime nanopb vendorizado: 0.4.9.1, licença em nanopb/LICENSE.txt.
- Protocolos de referência e licença GPL-3.0: reference/.
  Preservar avisos e revisar licenciamento ao extrair/publicar o módulo.

Referências oficiais:
https://meshtastic.org/docs/development/device/client-api/
https://github.com/meshtastic/firmware/tree/v2.7.26.54e0d8d/src/mesh
https://github.com/meshtastic/protobufs/tree/6b1ded439633cd03d4af85b44231b91d1d106278
https://github.com/nanopb/nanopb/tree/0.4.9.1

## Teste de interoperabilidade

Executar tests/test_wire.py com Python contendo o pacote oficial meshtastic.
Compara bytes produzidos pelo nanopb com ToRadio Python, decodifica FromRadio
Python e testa ruído/fragmentação, rejeição de comprimento >512 e protobuf
truncado. Usa Clang com sanitizers de memória e comportamento indefinido.
Não substitui testes de integração UART ou rádio.

## Testes de máquina de estados

`python3 firmware/tests/meshtastic_client/test_client_state.py` (na raiz do projeto consumidor) compila o cliente real, callbacks reais do
sensor e manutenção USB com UART/relógio simulados, ASan/UBSan. Repetir com
`--operation` para o perfil de operação. Não simula o escalonador, driver USB
real ou consumo; validar esses aspectos em hardware. Testes wire, diagnostics
e os testes do concentrador no consumidor permanecem complementares.

## Snapshot e separação de apresentação

`mc_snapshot_read(struct mc_snapshot *)` exporta uma cópia de tamanho limitado
com estado do rádio, validade, geração, contadores UART, flag de atualização e
instantes monotônicos de captura/conclusão do snapshot. Não consulta UART, não
aloca, não imprime, não inicia refresh e não altera a sessão. Retorna -EINVAL
para NULL. A chamada pertence à mesma thread dona de mc_poll/send/refresh; depois
da cópia, o consumidor pode receber o valor por uma fila de propriedade explícita.
Os contadores de bytes/erros produzidos na ISR são amostrados individualmente;
não se promete atomicidade global entre esses contadores e frames processados.

Os dados não contêm ponteiros, PSKs, senhas nem o Protobuf bruto. Os nomes são
copiados; nenhuma estrutura interna fica emprestada ao consumidor. Antes do
primeiro snapshot válido, initialized/synced são falsos. Durante refresh, a cópia
continua mostrando a última geração completa com refreshing=true. Após uma perda
real, synced=false sinaliza que os dados existentes não provam conexão atual.

`mc_diag_log` foi removido da API/implementação. diagnostics.h permanece apenas
como include de compatibilidade para os tipos, encaminhando a snapshot.h.
A formatação periódica agora fica em firmware/support/meshtastic_report.c,
compilado pelas aplicações e compartilhado por sensor e CONC. Não é dependência
de build do módulo cliente. Traces de protocolo/erros do cliente permanecem separados e configuráveis, via
rt_log (tag MESH), e não são o relatório periódico. Não há printk direto.

O adaptador atual captura na thread dona e publica cópias ao
rt_diagnostics_reporter. Formatação específica de rádio fica no callback da
aplicação; o reporter e o coletor permanecem independentes de Meshtastic.
rt_log, rt_diagnostics e rt_diagnostics_reporter são repositórios locais em
ZephyrRTOS/shared/modules, acessados pelo projeto por links de integração.

## Pendências atuais

Ver TODO.md deste módulo. RX, TX e espera são orientados a eventos. Contratos,
exemplo mínimo e revisão de proveniência foram concluídos em 30/10/2026.
Os ensaios físicos extremos e SENS02 não são substituídos por testes host.

## TX por interrupções

mc_send retorna 0 quando copiou/enfileirou o frame; -EAGAIN quando a capacidade
está cheia e mantém *packet_id intacto em falha. Não significa que o rádio recebeu
ou transmitiu. Os slots incluem frames já aceitos pela FIFO, ainda aguardando
reap pelo dono: isso limita também o backlog de conclusões, sem perder eventos.
Chamar mc_poll regularmente. Não reutilizar uma instância em voo por mc_init:
retorna -EBUSY enquanto existem slots pendentes. Init requer UART quiescente.

MC_TX_DRAINED chega somente por mc_poll, nunca da ISR ou dentro de mc_send.
status=0 significa todos os bytes entregues à FIFO/driver; não significa wire idle,
UART recebida pelo ESP32, QueueStatus, ACK ou processamento remoto. Em erro,
status é errno negativo e length informa bytes aceitos até então, incluindo o
cabeçalho. Packet_id correlaciona a mensagem de aplicação. Frames de controle e
padding usam id 0 e não geram esse callback público. Resultados já concluídos
antes de uma falha não são convertidos retroativamente em confirmação de entrega.

| Estágio | Evidência |
|---|---|
| Fila local aceitou | mc_send=0; log UART_ENQUEUED |
| Driver aceitou os bytes | MC_TX_DRAINED=0; log UART_TX_DRAINED |
| Rádio informou fila | MC_QUEUE/QueueStatus; log RADIO_QUEUE |
| Roteamento confirmou | MC_ROUTE status=0; log ROUTING |
| Aplicação remota processou | Não garantido por esses eventos; depende da aplicação |

A ISR faz uma única chamada limitada a uart_fifo_fill por serviço TX. FIFO parcial
preserva offset, bytes e ordem. Cada slot pertence à fila até ser reapado, mesmo
após última cópia ao driver; nenhum ponteiro de payload do chamador é retido.
Enable TX fica fora do spinlock, permitindo o gerenciamento de runtime-PM do driver.
A IRQ é desligada quando a fila foi entregue ou ocorre erro. Há wake do dono no
fim/erro de frame e por RX, não por todo fragmento TX. Não cria thread/timer.

## Limites, timeout e recuperação

MESHTASTIC_TX_QUEUE_DEPTH: 2..16, padrão 4. Cada slot guarda 516 bytes mais
length/offset/id/status/alinhamento. O mínimo 2 permite admitir padding e pedido
de configuração como um lote indivisível. Codificação Protobuf ocorre fora do lock;
cópia curta da fila e metadados são protegidos por spinlock. Não é lock-free.

MESHTASTIC_TX_TIMEOUT_MS: 100..60000, padrão 2000. Limita intervalo sem progresso
FIFO, não a duração total de um envio que progride. Deve exceder tempo de um bloco
do driver no baud escolhido e atrasos de escalonamento. mc_wait inclui esse prazo;
não há sondagem periódica. API IRQ é a única utilizada, sem misturar UART async.

Se TX anuncia ready mas fifo_fill não progride ou falha, a IRQ é desligada e o
dono trata a falha. No timeout, slots são cancelados com resultado explícito,
sessão é invalidada e sincronização volta com backoff 20..300 s por padrão,
não num laço ocupado. Aplicação pode ter dados já enviados: cancelamento não é
transação nem permite afirmar que o destino não recebeu. Não há retransmissão
automática de pacotes da aplicação.

Após erro de transporte/reboot, antes do próximo pedido de configuração, um lote
admite padding de 516 zeros e o frame de sincronização. Na versão de protocolo
fixada, isso esgota qualquer recepção parcial com comprimento máximo 512 e deixa
o parser procurando framing. Não se presume timeout do parser remoto: o caminho
serial dessa tag não limpa rxPtr apenas por silêncio. Esse tráfego UART extra é
limitado a recuperação, não gera consultas periódicas adicionais nem garante
rollback de efeitos que o rádio já tenha processado. Fonte consultada:
https://github.com/meshtastic/firmware/blob/v2.7.26.54e0d8d/src/mesh/StreamAPI.cpp

Uma recusa local de fila não troca nonce/staging nem sinaliza refresh iniciado.
Tentativas automáticas recusadas recebem prazo futuro, evitando mc_wait com prazo
vencido em laço. RX/erro de framing de entrada continuam sendo tratados enquanto
TX progride. Um refresh saudável mantém sessão e ACK pendente, como antes.

UART_TX_STATS expõe enqueued/drained/failed/rejected/bytes/pending, saturantes.
Contadores incluem unidades de controle/padding; bytes são aceitação FIFO, não
bytes confirmados no rádio. pending inclui slots concluídos ainda não reapados.
São amostrados juntos sob lock, sem alterar os contadores RX cumulativos.

## Baixo consumo desde o código

A CPU pode dormir enquanto o periférico transmite, sem uart_poll_out ocupado.
Não acorda o dono em cada bloco TX. No alvo nRF atual, o buffer EasyDMA do driver
foi configurado pelas aplicações em 128 bytes (antes 32), custando 96 bytes extras
de RAM e reduzindo o número de blocos/serviços TX necessários para o mesmo frame.
Essa configuração específica não entra no núcleo genérico. O objetivo é reduzir
trabalho/despertares, sem alegar uma corrente ou percentual não medidos.

Não se adiam melhorias por falta de instrumentos. Autonomia poderá ser comparada
em campo; UART/radio/ESP32 continuam sujeitos às suas próprias políticas de energia.
Não há promessa de deep sleep, wake/ready GPIO ou economia do conjunto nesta API.

## Regressões do TX

python3 tests/test_tx.py exercita capacidades 2/4, cópia antes da IRQ, FIFO parcial,
ordem/ownership, fila cheia, admission atômica, cancelamento, framing parcial,
no-progress, timeout e conclusão em IRQ imediatamente após enable. ASan/UBSan e
warnings como erros. Double manual de UART, não uma medição elétrica/escalonador.

python3 firmware/tests/meshtastic_client/test_client_state.py --tx-depth=2 e a variante --operation exercitam
RX em paralelo ao TX pendente, callbacks somente em poll, nonce intacto em recusa,
reinit ocupado, prazos de espera, recuperação de timeout/falhas repetidas e backoff.
Fonte testada é o cliente real com callbacks reais do sensor, não reimplementação
substituta. Validação do enlace físico desta revisão fica explícita no projeto.

## Integração independente

Adicionar este módulo e rt_log a ZEPHYR_EXTRA_MODULES antes de find_package(Zephyr).
Também pode ser usado como módulo West com zephyr/module.yml; respeitar a
ordenação/dependência rt_log. Não são necessários rt_dht, rt_diagnostics,
rt_diagnostics_reporter, sensor, concentrador ou support. Zephyr deve oferecer
SERIAL, UART_INTERRUPT_DRIVEN, entropia (sys_rand32_get), semáforos, ring, atomics
e spinlocks. O consumidor habilita LOG/CBPRINTF_COMPLETE e RT_LOG para logs.
Placa, pinctrl, baud, alimentação e UART exclusiva pertencem ao consumidor.

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES "/caminho/meshtastic_client" "/caminho/rt_log")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

O exemplo completo [examples/minimal](examples/minimal/README.md) usa só essas
dependências. Versões compiladas: Zephyr 4.4.99-ab92232cfcd e SDK 1.0.1;
compatibilidade com outro driver exige UART interrupt-driven com FIFO/IRQ
corretos. Não misturar API async com interrupt-driven na mesma UART.

## Ciclo de vida e concorrência

Um singleton por imagem, uma única thread dona. Sem API de deinit, múltiplas
instâncias ou sincronização entre dois produtores. UART e callback sobrevivem
à sessão. Não compartilhar UART com console ou outro callback. A aplicação
chama init/poll/wait/send/refresh/snapshot e mantém os prazos próprios; IRQ só
insere bytes, serve FIFO e sinaliza wake. Callbacks são síncronos e não
reentrantes: copiar dados e retornar; não chamar init/poll/send/refresh dentro
deles. Formatação pesada pode ir a consumidor com cópias/fila limitada.

| Estado | Comportamento e transição |
|---|---|
| Antes de init | ready=false, local=0, refresh=-ENODEV, send=-ENOTCONN; poll no-op |
| Sincronizando | Pedido aceito, staging privado; configuração correlacionada completa publica |
| Pronto | Recebe/envia; heartbeat e liveness; snapshot por cópia |
| Refresh | Pronto anterior preservado; nova geração só após resposta completa |
| Recuperação | Sessão invalidada; aborta TX, padding+config após backoff limitado |

Snapshot completo exige id local, User local, algum canal, firmware e seção
LoRa. Seções opcionais ausentes ficam identificadas pelas flags *_received;
um commit não afirma completude de toda configuração nem de todos os nós.
Um refresh com resposta incompleta mantém geração anterior e timeout/backoff.
MC_SNAPSHOT_BEGIN precede replay MC_METADATA/MC_CHANNEL/MC_NODE e MC_SYNCED;
NODEINFO pode emitir MC_NODE fora desse replay. MC_ERROR nem sempre desconecta.

Reinit exige UART quiescente e nenhum slot TX, inclusive concluídos ainda não
reapados. -EBUSY preserva sessão. NULL/indisponível retorna -ENODEV; falha de
registro do callback preserva sessão anterior e restaura RX. Após esses erros,
repetir init é permitido. Init aceito substitui callback e sessão, limpa ring,
parser, cache e estatísticas TX; não espera sincronização. Não é ressincronização
segura de parser remoto em voo: para sessão existente preferir refresh, não reinit.
Se envio inicial falhar, IRQs são desabilitadas e cliente fica sem sessão ativa.
mc_wait funciona antes de init; maximum_ms<=0 não espera; mc_wake é ISR-safe.
local_node pode manter último id após desconexão: consultar ready para validade.

## Contadores, limites e memória

RX bytes/frames/decode_errors/packets/decoded/not_decoded/routing/dropped_bytes
são cumulativos desde boot, uint32 com wrap; uart_error_mask é OR desde boot.
Erros de cliente são por init aceito, com wrap, preservados entre commits.
TX enqueued/drained/failed/rejected/bytes são por init aceito e saturam em
UINT32_MAX. pending é ocupação atual incluindo conclusões não reapadas. A leitura
RX não é uma transação global com ISR; TX é uma cópia coerente sob spinlock.
Geração é uint32 com wrap; timestamps são uptime monotônico int64 em ms, exceto
last_heard de cada nó (Unix seconds do rádio). Zero pode significar ausente.

Ring RX 4096 bytes, parser/encoding 512 bytes, fila depth*(516+metadados), cache e
staging de tamanho fixo mais Protobufs estáticos. Snapshots por valor incluem
nodes[CONFIG_MESHTASTIC_NODE_CACHE_SIZE] e oito canais: mais nós também aumentam
RAM das cópias do consumidor. Sem malloc. Cache cheio não expulsa nó: rejeita
atualizações de novos ids e incrementa dropped_node_updates saturante. Strings
são truncadas com NUL, sem prometer caracteres UTF-8 completos no limite.

A ISR pode drenar várias leituras RX; poll drena o ring disponível e faz decode/
callbacks. Não há WCET fixo ou garantia hard real-time; callback lento aumenta
risco de overflow. Reap TX é limitado por depth por poll, cópia sob lock é limitada
ao frame, sem espera por slot ou USB. O consumidor deve dimensionar seu ritmo.

## Opções Kconfig e políticas

| Opção do cliente | Padrão / unidade | Efeito e custo |
|---|---|---|
| CLIENT | y, condicionado a SERIAL/IRQ | Compila cliente e buffers; requer rt_log |
| NODE_CACHE_SIZE | 32 nós, 1..128 | RAM de cache/staging/cópias |
| TX_QUEUE_DEPTH | 4 slots, 2..16 | RAM e backlog limitado de resultados |
| TX_TIMEOUT_MS | 2000 ms, 100..60000 | Falta de progresso FIFO, não duração total |
| PACKET_TRACE | n | DBG por pacote, custo CPU/log quando habilitado |
| REFRESH_INTERVAL_MS | 0 ms, 0..3600000 | Zero desliga consulta periódica |
| HEARTBEAT_INTERVAL_MS | 60000 ms, 10000..300000 | Keepalive UART sem payload mesh |
| LIVENESS_TIMEOUT_MS | 180000 ms, 30000..900000 | Deve exceder heartbeat |
| SYNC_TIMEOUT_MS | 20000 ms, 1000..60000 | Prazo inicial de resposta/recuperação |
| RETRY_MAX_MS | 300000 ms, 60000..900000 | Teto de retry contínuo limitado |
| WANT_ACK | y | Atributo do pacote, custo de confiabilidade no rádio |
| HOP_LIMIT | 1, 0..7 | Limita saltos dos pacotes enviados |

Todos os nomes acima têm prefixo MESHTASTIC_. O suporte das duas aplicações
mantém MESHTASTIC_DEBUG_PROFILE, STATUS_INTERVAL_MS, DISCOVERY_INTERVAL_MS e
ACK_TIMEOUT_MS, preservando configurações existentes. Debug das aplicações
mantém trace/refresh padrão 60 s e status 5 s; operation.conf preserva refresh=0,
status=60 s e descoberta=300 s. Separar opções não mudou esses intervalos.

## Formato serial e compatibilidade

Header 94 c3, comprimento unsigned de dois bytes big-endian, payload Protobuf
ToRadio/FromRadio de até 512 bytes. Conteúdo Protobuf segue wire format oficial
(campos varint/fixed/length-delimited), não layout de struct C. Payload de aplicação
até 200 bytes é opaco para este cliente; não adiciona JSON, CRC ou versão própria.
Parser tolera ruído e header parcial, rejeita comprimento >512 e o chamador
reinicia parcial após 2000 ms. Não há checksum adicional no framing serial.

want_config_id é nonce uint32 não zero, diferente dos reservados 69420/69421;
config_complete_id precisa coincidir e staging precisa estar completo. Heartbeat
usa nonce zero. Mensagens desconhecidas decodificáveis atualizam liveness mas
não geram evento. Sem migração/regeneração automática: descritores são fixados
na versão citada e versões diferentes precisam de teste de interoperabilidade.

## Erros e diagnóstico

Consultar contratos completos em client.h, snapshot.h e frame.h. -EAGAIN não
significa envio; packet_id de saída fica intacto. QueueStatus, FIFO e Routing
são estágios separados. Sem ACK não se pode afirmar perda nem processamento.
Timeout de aplicação não retransmite automaticamente. UART overflow/erro,
reboot e liveness invalidam sessão; frame/protobuf inválido isolado não apaga
snapshot saudável. Sem chave nos snapshots/logs do cliente; não imprimir
Protobuf bruto nem credenciais no consumidor.

| Sintoma | Conferência |
|---|---|
| init=-ENODEV | Dispositivo pronto, alias, status/pinctrl e alimentação |
| init=-ENOTSUP/erro de callback | Driver com interrupt-driven, exclusividade UART |
| init=-EBUSY | Rodar poll para reap; não reiniciar sessão com TX em voo |
| Sem sync | TX/RX/GND/baud, rádio com API serial, snapshot completo e nonce |
| -EAGAIN | Fila limitada ou backoff; aguardar eventos/prazo, não girar em loop |
| dropped_bytes/decode_errors | Ritmo do dono/callbacks, ruído UART e bursts |
| ACK sem RX da aplicação | ACK só comprova resultado de roteamento, revisar receptor |

## Testes independentes e integração

`python3 tests/test_lifecycle.py` testa cliente real sem fonte de sensor/CONC/USB:
pré-init, NULL/dispositivo inválido, falha de registro, reinit ocupado, preservação
em falha, callback NULL e contadores cumulativos/resetados. Junto com
`test_tx.py`, `test_diagnostics.py` e `test_wire.py` constitui a suíte independente.
ASan/UBSan onde indicado pelos runners; wire requer pacote Python oficial
meshtastic. Testes host usam doubles de UART/tempo, não placa/energia.

`firmware/tests/meshtastic_client/test_client_state.py` no consumidor é regressão adicional de **integração da
aplicação**: callbacks reais do sensor, ACK pendente, suporte USB e relatórios.
Não é requisito para construir/testar o cliente isolado. Executar debug/operação
e --tx-depth=2. `python3 tests/test_headers.py` verifica headers C11/C++17 e declarações com linkage C.
Os exemplos e consumidores devem ser compilados na árvore, sem flash automática.

## Remoções e proveniência

mc_diag_log foi removido; diagnostics.h encaminha apenas snapshot.h. Apresentação
permanece no consumidor. Nenhuma API pública foi removida nesta revisão; nomes
Kconfig de políticas foram preservados nas aplicações. Ver [NOTICE.md](NOTICE.md)
e [PROVENANCE.json](PROVENANCE.json): não atribuir Apache-2.0 ao pacote vendorizado
inteiro só porque os nossos fontes têm esse identificador.


## Portabilidade de microcontrolador

Nenhum fonte do cliente chama nrfx, HAL Nordic, GPIO nRF, USB ou bootloader.
O transporte usa device/UART interrupt-driven, kernel/semáforo/spinlock,
ring/atomics e sys_rand32_get do Zephyr. Assim o mesmo código atende qualquer
papel de nó e outros microcontroladores **com essas APIs/driver**. Não é uma
biblioteca bare-metal nem POSIX: portar para outro RTOS exige adaptar a camada
Zephyr; independência de placa não significa independência do sistema operacional.

O exemplo inclui overlay de compilação qemu_cortex_m3, com cliente na UART1 e
console na UART0. Esse alvo não Nordic permite conferir compilação sem headers
nRF; não comprova comunicação física com rádio nem comportamento de todo driver.
O ajuste EasyDMA 128 bytes é exclusivo da configuração das aplicações nRF,
sem condicional Nordic no cliente. Em outro alvo, revisar FIFO, IRQ, baud,
runtime-PM e energia do conjunto sem inserir pinagem/identidade no módulo.
