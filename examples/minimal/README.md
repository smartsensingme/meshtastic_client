# Cliente mínimo

Exemplo Zephyr sem sensor, concentrador, manutenção USB, telemetria ou reporter.
Só sincroniza, recebe e registra eventos via rt_log. Não provisiona rádio nem
transmite payload periódico. Heartbeat/liveness continuam necessários à sessão;
refresh periódico e packet trace ficam desligados para reduzir tráfego/logs.

Dependências: Zephyr 4.4.99-ab92232cfcd (versão validada neste projeto), toolchain
Zephyr SDK 1.0.1, módulo rt_log e UART com API interrupt-driven exclusiva, além de
entropia. Runtime nanopb e descritores já acompanham o cliente. O módulo rt_log
é procurado como irmão; use RT_LOG_MODULE_DIR para outra localização.

Forneça overlay da sua placa com alias meshtastic-uart apontando para UART
habilitada, baud/pinctrl e conexão GND/TX/RX corretos. Console/log não pode usar
essa mesma UART. O cliente não define pinos ou console. Exemplo de fragmento
(aplicar pinctrl específico da placa separadamente):

```dts
/ { aliases { meshtastic-uart = &uart0; }; };
&uart0 { status = "okay"; current-speed = <115200>; };
```

```sh
west build -b SUA_PLACA -d build-mesh examples/minimal -- \
  -DDTC_OVERLAY_FILE=/caminho/placa.overlay \
  -DRT_LOG_MODULE_DIR=/caminho/rt_log
```

Execute a partir da raiz deste módulo. No projeto nRF atual, o overlay da aplicação
pode ser reutilizado para **compilar** o exemplo; este exemplo não inclui a
manutenção bootloader nem reproduz todos os ajustes de USB da aplicação.
Não gravar automaticamente para testar compilação.

A thread dorme em mc_wait até RX/TX/deadline. Callbacks não fazem consultas ou
chamadas reentrantes. Para enviar, após mc_poll e mc_ready, chame mc_send com
unicast explícito; veja client.h e a tabela de confirmações no README principal.
A aplicação continua responsável por validar identidade/canal/firmware e por
interpretar o payload. RX/ACK não são prova de processamento remoto.

## Compilar sem hardware Nordic

```sh
west build -b qemu_cortex_m3 -d build-mesh-qemu examples/minimal
```

O overlay usa UART1 para cliente e mantém console em UART0. O arquivo .conf
específico desse alvo habilita gerador aleatório de teste por ausência de entropia
física; Zephyr emite aviso esperado. Este perfil é só para compilação/simulação,
nunca fonte de chaves. O cliente usa aleatoriedade para ids/nonce de correlação,
não criptografia; criptografia fica no rádio Meshtastic. Não executar nem gravar
esse exemplo como substituto do firmware operacional.
