# Proveniência e avisos

Revisão de fontes em 30/10/2026. Este pacote contém código de origens distintas;
o identificador Apache-2.0 dos fontes próprios não relicencia dependências.
Nenhum remoto/publicação foi criado nesta revisão.

| Conjunto | Origem fixada | Aviso preservado |
|---|---|---|
| src, include, exemplos e novos testes próprios | Este projeto | SPDX Apache-2.0 nos fontes |
| generated/meshtastic | Firmware Meshtastic v2.7.26.54e0d8d, src/mesh/generated/meshtastic | Cabeçalhos do gerador nanopb preservados; generated/LICENSE copia licença upstream GPL v3 |
| reference | Protobufs Meshtastic 6b1ded439633cd03d4af85b44231b91d1d106278 | reference/LICENSE, GPL v3 |
| nanopb | Runtime 0.4.9.1, Petteri Aimonen | nanopb/LICENSE.txt, aviso permissivo de três condições |

Fontes oficiais verificadas:

- [Firmware e licença](https://github.com/meshtastic/firmware/tree/v2.7.26.54e0d8d).
- [Protobufs e licença](https://github.com/meshtastic/protobufs/tree/6b1ded439633cd03d4af85b44231b91d1d106278).
- [nanopb e licença](https://github.com/nanopb/nanopb/tree/0.4.9.1).

PROVENANCE.json registra URL e SHA-256 local/upstream para os 29 arquivos
vendorizados originais: todos comparados byte a byte, sem divergência nesta
revisão. Os .pb.cpp upstream têm extensão local .pb.c para compilação C, sem
alteração de conteúdo. O snapshot local contém só mesh.proto e portnums.proto
como referência, não todos os insumos necessários à regeneração. Para regenerar,
recuperar o conjunto completo de schemas/options e ferramenta da versão fixada.

Os avisos existentes não foram removidos nem os fontes de terceiros formatados.
A classificação acima registra os avisos e a origem observados, sem afirmar uma
licença permissiva única para o conjunto. Em extração/distribuição preservar
licenças, proveniência, alterações e os requisitos aplicáveis ao código derivado.
