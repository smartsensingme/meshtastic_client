# meshtastic_client — backlog consolidado

Revisão: 30/10/2026, após concluir contratos e extrair o cliente como módulo
Zephyr independente em shared/modules/meshtastic_client. O consumidor mantém
seu histórico e ensaios específicos no TODO da aplicação.
Não considerar checkboxes antigos como prova de ausência de implementação.

## Já implementado e coberto

- [x] UART fornecida pela aplicação, RX por IRQ e ring limitado de 4096 bytes.
- [x] Framing 94 c3 + tamanho big-endian, limite 512, fragmentação/ruído,
  comprimento inválido, timeout de frame parcial e ressincronização.
- [x] Protobuf/nanopb fixados conforme README; mc_send unicast e metadados RX.
- [x] Estado/snapshot por staging e nonce correlacionado; refresh não desconecta
  sessão saudável nem apaga ACK pendente da aplicação.
- [x] Espera por semáforo/prazos, wake retido, heartbeat neutro, liveness,
  backoff limitado em frequência e atualização por NODEINFO.
- [x] QueueStatus separado de routing ACK; ACK não prova processamento remoto.
- [x] Snapshot por valor, sem chave/ponteiro, apresentação fora do cliente.
- [x] rt_log genérico como dependência, sem pinagem/bootloader/JSON no cliente.
- [x] Testes reais de estado com callbacks do sensor, UART/tempo simulados,
  ASan/UBSan em debug/operação; framing/protobuf e cache testados separadamente.
- [x] Hardware: telemetria SENS01 -> CONC, ACKs e refresh observados; sem terminal
  aprovado pelo usuário. Isso não cobre todos os reinícios/falhas físicos.

## Prioridade 1 — transmissão UART sem espera ocupada

- [x] Substituir write_message/uart_poll_out byte a byte por TX orientado a
  interrupção ou API assíncrona compatível com o driver. RX já é assíncrono.
- [x] Definir fila/buffers próprios limitados, ownership do frame até conclusão,
  política quando cheio e indicação distinta de aceitação/enfileiramento e
  conclusão UART. Não confundir com QueueStatus/ACK do rádio.
- [x] Preservar ordenação de frames de aplicação, heartbeat e sincronização;
  não modificar nonce/estado como se um frame recusado já tivesse sido enviado.
- [x] Tratar TX parcial, conclusão, erro e recuperação sem reutilizar memória
  em voo; sinalizar trabalho por eventos, sem polling ou sleeps frequentes.
- [x] Testes determinísticos de FIFO parcial/cheia, concorrência RX/TX, falha,
  reboot, refresh com envio em voo e ausência de corrupção/espera ocupada.
- [x] Compilar sensor/CONC debug/operação na árvore; implementar baixo consumo
  desde o código, sem aguardar instrumentos ou inventar economia quantitativa.
- [x] Enlace nominal da implementação IRQ observado no ensaio irq-tx de
  29/10/2026, conforme TODO do consumidor; não cobre cenários físicos extremos.
- [x] Validar operação nominal da revisão de contratos/extração: baseline de
  aproximadamente 5min30s, 11 TX/RX/ACK correlacionados, seis sincronizações por
  placa e nenhum erro observado. Falhas físicas abaixo continuam pendentes.
  Evidência no consumidor: docs/mesh-physical-2026-10-30/RESULTADO.md.

Implementado em 29/10/2026: fila IRQ com quatro slots por padrão, resultados
retidos até poll, eventos MC_TX_DRAINED e timeout orientado ao próximo prazo.
O driver nRF usa blocos de 128 bytes; não há espera ocupada nem nova thread/timer.
Ver README e docs/TX_IRQ.md da aplicação para semântica e limitações.

## Prioridade 2 — contrato, configuração e independência

- [x] Aplicar documentação completa do perfil opt-in ao cliente quando entrar
  nessa etapa: enums/eventos, parâmetros, buffers, retorno, callbacks e estado.
- [x] Documentar limites de singleton/reinit e contadores cumulativos; verificar
  comportamento em erro de inicialização e chamadas antes de init.
- [x] Revisar Kconfig: separar opções do cliente das políticas de status,
  descoberta de destino e ACK tracking que hoje pertencem às aplicações.
- [x] Revisar proveniência e avisos de licenças de generated/reference/nanopb
  antes de preparar extração/publicação própria do cliente. Preservar avisos.
- [x] Definir exemplo mínimo sem sensor/CONC e dependências explícitas; não
  transformar as aplicações de telemetria em requisito dos testes do módulo.

## Validação física pendente

- [x] Reset real do rádio com ACK pendente: sessão perdida explicitamente,
  entrega desconhecida e recuperação automática em aproximadamente 7,46 s;
  três TX/RX/ACK posteriores e refresh confirmados em 30/10/2026. Não prova
  bytes UART em trânsito. Evidência no relatório físico do consumidor.
- [ ] Silêncio UART, desligamento/religamento e buffer sob rajada no hardware.
- [ ] Ensaio prolongado/múltiplos nós no consumidor (incluindo SENS02 quando
  disponível); não é dependência de implementação ou teste deste módulo.
- [ ] Medir custo/consumo do conjunto permanece adiado pelo usuário.

A desconexão do monitor USB do CONC no final do ensaio de stack deve ser
investigada no suporte/USB se reaparecer; não é evidência de defeito na UART do
cliente Meshtastic ou de overflow de stack. Não alterar o protocolo por suposição.

## Entrega independente — 30/10/2026

- [x] Nenhuma pinagem, HAL nRF, identidade de nó ou política de telemetria nos fontes.
- [x] Políticas debug/status/descoberta/timeout de ACK movidas ao Kconfig do consumidor,
  com nomes e padrões preservados; ACK/hop de pacote permanecem no cliente.
- [x] mc_poll antes de init é no-op; NULL/UART inválida rejeitados; falha de registro
  de IRQ não deixa sessão parcial nem destrói sessão anterior em reinit recusado.
- [x] Headers C11/C++17, contratos de callback, singleton/reinit, memória e contadores.
- [x] Suíte independente com nó/porta arbitrários, sync/refresh/nonce, reboot durante
  TX, silêncio/overflow simulados, além de FIFO/framing/cache/protobuf. ASan/UBSan.
- [x] Testes de sensor/ACK/USB movidos ao consumidor; não são dependência do módulo.
- [x] Exemplo mínimo compilado em qemu_cortex_m3 (TI, sem Nordic), UART exclusiva.
- [x] Cópia isolada com nome diferente testada, rt_log explícito, sem aplicações.
- [x] Proveniência SHA-256 dos 29 arquivos terceiros conferida com upstream;
  licenças preservadas, NOTICE distingue fontes próprios e dependências.
- [x] Integração por link e repositório Git local sem remoto/publicação.

A portabilidade é entre alvos Zephyr com driver UART interrupt-driven compatível,
não entre sistemas operacionais. O build QEMU não valida driver/rádio físico.
Medição de consumo continua adiada; testes de falhas físicos continuam abertos.
Relatório no consumidor: docs/MESHTASTIC_CLIENT_INDEPENDENTE.md.
