# AUDITORIA R38 — source v1.0.82-final-audit (base R37)

**Data:** 19/10/2026 (America/Sao_Paulo). **Escopo auditado:** somente o ZIP enviado na conversa, sem repositório Git interno. **Natureza:** source corrigido; não é uma certificação de EFI compilado para Release.

## Resumo executivo e status real

- **Status do source:** pacote completo derivado dos 21 arquivos originais R37; após a correção do gate foram modificados o arquivo C (correções R38 anteriores) e um teste original, foi adicionado um arquivo de testes, e foram consolidados este relatório e o patch. Os outros 18 arquivos originais permaneceram byte a byte idênticos; o log de execução original foi substituído por evidência atual. As permissões executáveis originais dos scripts Python foram restauradas.
- **Bugs comprovados e corrigidos:** (1) `ForcePin` ignorado pelo fallback de perfil AppleALC; (2) `ForcePin` ignorado pelas rotas alternativas da recuperação; (3) fanout Realtek legado podia programar rotas adicionais sem snapshot transacional prévio.
- **Evidência antes da correção:** os dois testes C extraídos diretamente das implementações retornaram erro (11 e 20); a regressão de snapshot também falhou por ausência da chamada.
- **Evidência depois:** 270/270 testes de source aprovados, incluindo dois testes de execução real de funções C compiladas com I/O HDA simulado e uma regressão de snapshot; execução adicional 270/270 com Python development mode e allocator de depuração.
- **Build de Release:** **NÃO VALIDADO**. Depois do ajuste do teste, o comando de build foi executado em cópia isolada com cinco rodadas reais de 270/270 testes, todas aprovadas, e encerrou na ausência de workspace EDK2. Não houve compilação DEBUG/RELEASE nem `.efi` novo. O único log de build do pacote foi atualizado com essa execução; o log R37 anterior continua disponível no ZIP R37 fornecido.
- **Firmware/hardware:** PCI MMIO, DMA, áudio real, timing de Boot Services/ExitBootServices, eventos TPL e codecs físicos não foram executados neste host. Nenhuma afirmação de ausência total de bugs ou cobertura completa é feita.
- **Versão declarada no produto:** `1.0.82-final-audit` preservada, assim como README, CHANGELOG, `.inf`, `.dsc`, configurações, assets e cabeçalhos gerados. O R38 designa somente esta auditoria do source; não altera o número comercial nem mascara os testes históricos R37.

## Identidade de origem e proveniência

- ZIP fornecido: `HDAUniversalBootChimeDxe-BootChimeMP3-v1.0.82-FinalAudit-R37-FULL-SOURCE-2026-09-20.zip`.
- SHA-256 ZIP original: `7149cc93193fe7cffc80262b493098e9de52940d8b048c66e0aa272f7b91f20f`.
- O ZIP não contém `.git`, refs nem commits do projeto; não é possível comprovar commits remotos ou afirmar que este é o HEAD de produção independente do arquivo fornecido.
- O build referencia `edk2-stable202608` e commit esperado `2970e5699ba6267f3384ffab20f96647578aebc8`; esse commit é uma configuração local, NÃO uma verificação de checkout executada aqui.
- O log histórico do ZIP **R37** terminava com **267/267 testes**, seguido de ausência de EDK2. No pacote **R38 atualizado**, o único `Release/Logs/BUILD-COMPILE.log` é o registro posterior das cinco rodadas de 270/270 testes e da ausência de EDK2; não representa um EFI compilado.

## Mudanças no código, causa raiz e comprovações

1. **Fallback AppleALC (`HdaTryAlcIdProfilePath`)**: o loop dos pares de pin/DAC priorizava as sugestões da tabela e não testava `mHdaCfg.ForcePin`. Quando a rota exata não resolvia, era possível selecionar outro pino. Adicionado `if` de exclusão apenas quando existe pin forçado. Um harness compilando literalmente essa definição reproduziu a falha antes e passou depois; também confirma comportamento sem ForcePin.
2. **Fallback de candidatos (`HdaCollectRouteCandidates`)**: a enumeração das rotas adicionais usava prioridade/validade de pins, mas não restringia `ForcePin`. Uma falha da rota primária podia migrar de pino. Adicionado filtro antes de qualquer consulta de capacidade para cada candidato. Harness C real provou antes/depois e a preservação de rotas quando o override está desligado.
3. **Fanout legado (`HdaEnableLegacyRealtekFanout`)**: as rotas de saída extras eram programadas depois do snapshot do caminho primário, sem `HdaSnapshotPathNodes` adicional (diferentemente dos fanouts AppleALC e Realtek combo). Adicionada captura antes de escrever registradores, apenas se `TransactionalRestore=1`, com skip seguro quando o snapshot falha. A correção tem evidência de análise de fluxo e regressão de ordem/guardas, mas NÃO um teste HDA de rollback físico.

**Política de mínima alteração:** nenhum dado PCM, layout AppleALC, código de build, flags de configuração ou interface foi modificado. Não houve criação de `.efi` fictício. Arquivos de auditoria foram centralizados nesta pasta; só existe **um patch** novo em `Audit/diffs/`.

## Rodadas reais realizadas

| Rodada | Método | Resultado | Limitação |
|---|---|---|---|
| Base fornecida | `python -B -m unittest discover -s Tests -p test*.py -q` | 267/267 OK | source antes das novas correções; sem EDK2 |
| Regressão dirigida pré-fix | compilação/execução das funções C `HdaTryAlcIdProfilePath`, `HdaCollectRouteCandidates` com mocks | 2 falhas reprodutíveis (retornos 11/20) | harness não é codec físico |
| Inspeção independente de snapshot | verificação de guardas e ordem de `HdaEnableLegacyRealtekFanout` | Falhou antes; prova falta de captura | verificação estática do corpo real |
| Correção A | source C modificado; `unittest` | 270/270 OK | validação no host |
| Revisão focada de invariantes | conferência dos chamadores/fanouts/ForceDac; `unittest` | 270/270 OK | revisão por risco, não total |
| Pipeline em cópia isolada | build `.command` com `SOURCE_TEST_ROUNDS=3`, `AUTO_FETCH_EDK2=0` | rodadas 1 e 2 completas (270/270 cada); terceira interrompida por limite de execução do ambiente | não contabilizar 3ª rodada do pipeline como concluída |
| Reexecução independente | `python -B -m unittest` no diretório isolado | 270/270 OK | não é a conclusão retroativa da rodada interrompida |
| Gate final real | build `.command`, `SOURCE_TEST_ROUNDS=1`, `AUTO_FETCH_EDK2=0` | 270/270 OK; etapa EDK2 parou sem workspace | não compila EFI |
| Gate após correção do teste R38 | build `.command`, `SOURCE_TEST_ROUNDS=5`, `AUTO_FETCH_EDK2=0`, cópia isolada | **5/5 rodadas, 270/270 em cada**; EDK2 não instalado | Linux host; sem compilação EFI ou boot macOS |
| Python depuração | `PYTHONMALLOC=debug PYTHONHASHSEED=123 python3 -X dev -B -m unittest` | 270/270 OK | checks de host, não prova de ausência de leaks no DXE |
| Syntax/patch | `bash -n` nos 3 comandos; AST Python nos 3 arquivos; `git apply --check` e reprodução integral do patch | todos OK | sem compilação do driver completo |

## Análise de plataforma, riscos e condições extremas

- **Memória/DMA:** leitura dirigida dos fluxos de alocação/mapeamento, stop de stream, buffer retain, BUS_MASTER containment e restauração. As matrizes e testes existentes documentam ownership e fluxos de erro; o host não observa DMA real nem prova ausência universal de uso-após-liberação.
- **Concorrência e TPL:** examinado o contrato de timer, callbacks e bloqueios de reentrância, inclusive retentativa no boot handoff. Testes de source cobrem invariantes estruturais; interferências de firmware, interrupções e escalonamento real continuam fora do alcance.
- **Restauração do codec:** comparado snapshot da rota primária com fanouts AppleALC, combos e legado; identificado e corrigido o ponto divergente. É necessária captura de log e teste físico de antes/depois da reprodução para confirmar rollback do hardware.
- **AppleALC:** há perfis gerados e teste de consistência do generator, mas a origem externa dos arquivos AppleALC e sua versão histórica não consta do ZIP; não certificamos a equivalência a uma árvore remota independente.
- **Configuração/compatibilidade:** `ForcePin` somente restringe caminho quando explicitamente configurado; sem ForcePin o fallback permanece funcional nos mocks. Defaults `TransactionalRestore=1` e três flags de fanout legado desabilitadas nos `.ini` foram preservadas.
- **Janelas/GPU/UI:** inaplicáveis a DXE headless. GPU/energia e latências reais dependem de hardware não presente.

## Integridade dos PCM e conteúdo imutável

- `HDAUniversalBootChimeDxe/BootChimePcm.h`: 243252 amostras INT16 conferidas; mínimo=-28737, máximo=28432; payload idêntico ao R37.
- `HDAUniversalBootChimeDxe/BootChimePcm44.h`: 223488 amostras INT16 conferidas; mínimo=-28727, máximo=28422; payload idêntico ao R37.
- `assets/BootChime.mp3`: arquivo original byte a byte preservado. Integridade não implica equivalência exata a uma recodificação sem referência original de encoder.

## Inventário por arquivo com SHA-256 integral

`Lido` significa arquivo carregado/inspecionado ou indexado. `Analisado` significa inspeção funcional específica, não apenas hash/scan. `Validado` exige evidência explícita e é limitado ao método declarado.

| Arquivo | Linhas | SHA-256 R37 | SHA-256 agora | Situação / cobertura |
|---|---:|---|---|---|
| `Audit/diffs/R37-to-R38.patch` | 225 | `— novo` | `82b9d76f95960e2` | Patch único atualizado; aplicação em R37 e comparação byte a byte OK |
| `Build-HDAUniversalBootChimeDxe.command` | 900 | `d004d3105805ef92` | `d004d3105805ef92` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `CHANGELOG.md` | 417 | `2d26c0aa4ca11bf0` | `2d26c0aa4ca11bf0` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `Enable-HDAUCodecDump-NextBoot.command` | 243 | `c7f7cd4706733cd7` | `c7f7cd4706733cd7` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `HDAUChimeDebug.ini` | 59 | `fa7698619bf07ba1` | `fa7698619bf07ba1` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `HDAUChimeRelease.ini` | 59 | `901561bc324f5200` | `901561bc324f5200` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `HDAUniversalBootChimeDxe/BootChimePcm.h` | 20293 | `68220bf126b43627` | `68220bf126b43627` | Integridade amostras; bytes inalterados |
| `HDAUniversalBootChimeDxe/BootChimePcm44.h` | 18646 | `1da445f511019536` | `1da445f511019536` | Integridade amostras; bytes inalterados |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` | 10894 | `3e0a8eb9d8919520` | `2537228ca8964067` | Lido/indexado; revisão dirigida; testes C host; EFI PENDENTE |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf` | 45 | `222f8f58ce0e5190` | `222f8f58ce0e5190` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `HDAUniversalBootChimeDxe/HdaAppleAlcLayouts.h` | 3672 | `bd348bd45382a3b3` | `bd348bd45382a3b3` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `HDAUniversalBootChimeDxe/HdaUefi.h` | 577 | `b6357b966e78babe` | `b6357b966e78babe` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `HDAUniversalBootChimeDxePkg.dsc` | 33 | `5ada46b1cb0eba18` | `5ada46b1cb0eba18` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `README.md` | 608 | `f3d9a12ab86c078a` | `f3d9a12ab86c078a` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `Release/Logs/BUILD-COMPILE.log` | 1395 | `f8a583a15a044d14` | `74286f6cd614bac0` | Gate R38 atual, 5/5 × 270/270; EFI pendente |
| `Setup-EDK2.command` | 85 | `6a515d741b0ef47c` | `6a515d741b0ef47c` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `Tests/test_audit_regressions.py` | 4038 | `cb353f0686276612` | `b5f5c610cfb357ae` | Teste de higiene do patch corrigido; 270/270 × 5; inspeção integral PENDENTE |
| `Tests/test_forcepin_runtime.py` | 171 | `— novo` | `00d77fb92e43e0b9` | Novo; 3 testes executados; 2 funções C compiladas |
| `Tools/generate_applealc_profiles.py` | 764 | `25cf93fa400ab8bd` | `25cf93fa400ab8bd` | AST+regressões; revisão por função PENDENTE |
| `assets/BootChime.mp3` | binário | `7914bd202b1f539d` | `7914bd202b1f539d` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `docs/NO-SHELL-EFI-TEST.md` | 53 | `892dbf1611ea71e2` | `892dbf1611ea71e2` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `docs/PROJECT-REPORT.txt` | 370 | `24c7735d7506a1db` | `24c7735d7506a1db` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |
| `docs/TESTING.md` | 81 | `4adf5045d95beeb6` | `4adf5045d95beeb6` | Lido/indexado, hash conferido; revisão abrangente PENDENTE |

**Observação:** hashes são exibidos abreviados a 16 caracteres na tabela; SHA-256 integrais de origem estão armazenados na identidade de ZIP e em cada checkpoint indicado a seguir. O hash do próprio relatório não é auto-referenciado.

## Matriz de cobertura por função C (todas as definições identificadas)

- Funções C identificadas: **209** (208 `STATIC` + ponto de entrada EFIAPI). Linhas completas indexadas pelo parser de blocos balanceados, incluindo literais/comentários; a presença na matriz NÃO equivale a auditoria manual concluída.
- Revisão funcional dirigida explicitamente registrada em **55** funções; as demais requerem exame individual adicional. Teste em HDA real: **0** funções.

| Função | Linhas | SHA-256 bloco | Lido / análise | Validação e riscos | Chamadores internos (amostra) |
|---|---|---|---|---|---|
| `HdaArmDeferredCodecDumpTimer` | 217–224 | `d69c29cb95ca` | Lido/indexado; análise manual pendente | NÃO validada individualmente; TPL / callback / lifecycle | HdaDeferredCodecDumpNotify, HdaUniversalBootChimeDxeEntry |
| `HdaAsciiLower` | 226–229 | `77a356a74de7` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaAsciiEqualsInsensitive, HdaAsciiStartsWithInsensitive, HdaUniversalBootChimeDxeEntry |
| `HdaAsciiEqualsInsensitive` | 282–290 | `415bf7e23434` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaParseBoolStrict, HdaApplyConfigKey, HdaUniversalBootChimeDxeEntry |
| `HdaSkipSpaces` | 292–295 | `4612586cd2b0` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaApplyConfigKey, HdaParseConfigBuffer, HdaUniversalBootChimeDxeEntry |
| `HdaTrimRight` | 297–307 | `1f4c8ba64a14` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaApplyConfigKey, HdaUniversalBootChimeDxeEntry |
| `HdaParseNumberStrict` | 309–343 | `b067a70bf4c9` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaParseBoolStrict, HdaApplyConfigKey, HdaUniversalBootChimeDxeEntry |
| `HdaParseBoolStrict` | 345–364 | `33070a515857` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaApplyConfigBool, HdaApplyConfigKey, HdaUniversalBootChimeDxeEntry |
| `HdaApplyConfigBool` | 366–372 | `1e9e63292431` | Lido/indexado; análise manual pendente | NÃO validada individualmente; I/O / persistência / tratamento de erro | HdaApplyConfigKey, HdaUniversalBootChimeDxeEntry |
| `HdaAsciiToUnicode` | 374–381 | `3646efee24e5` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaSetNvramAscii, HdaUniversalBootChimeDxeEntry |
| `HdaSetNvramAscii` | 383–394 | `e6869fcba791` | Lido/indexado; análise manual pendente | NÃO validada individualmente; I/O / persistência / tratamento de erro | HdaAcquireBootGuard, HdaTraceStage, HdaSetLastStatus, HdaDumpAllControllers (+1) |
| `HdaCloseFileHandleTerminal` | 398–416 | `31a3b7d39797` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaReadConfigFromPath, HdaOpenDebugLog, HdaDebugInit, HdaEnsureAppendLog (+2) |
| `HdaCloseEventTerminal` | 418–436 | `03f1fb142fc5` | Lido/indexado; análise manual pendente | NÃO validada individualmente; TPL / callback / lifecycle | HdaDeferredCodecDumpNotify, HdaUninstallBootScreenDNotify, HdaCleanupAsyncPlayback, HdaAsyncPlaybackTick (+1) |
| `HdaCloseTrackedLogFile` | 438–466 | `fefd21cda4dc` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaLog, HdaOpenDebugLog, HdaDebugFinish, HdaCloseAppendLog (+2) |
| `HdaAcquireBootGuard` | 468–554 | `2aa903c4ccd4` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaUniversalBootChimeDxeEntry |
| `HdaLog` | 556–603 | `f1eaf715d11b` | Lido/indexado; análise manual pendente | NÃO validada individualmente; I/O / persistência / tratamento de erro | HdaAcquireBootGuard, HdaTraceStage, HdaSetLastStatus, HdaReadAlcIdFromBootArgs (+75) |
| `HdaTraceStage` | 607–624 | `0bd8b35e737b` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaPlayChime, HdaTryController, HdaUniversalBootChimeDxeEntry |
| `HdaSetLastStatus` | 626–629 | `d614d1754563` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDeferredCodecDumpNotify, HdaAsyncPlaybackTick, HdaStartFastBootPlayback, HdaPlayChime (+4) |
| `HdaApplyConfigKey` | 631–749 | `eb8b483c3187` | Lido/indexado; análise manual pendente | NÃO validada individualmente; I/O / persistência / tratamento de erro | HdaParseConfigBuffer, HdaUniversalBootChimeDxeEntry |
| `HdaParseConfigBuffer` | 751–779 | `6b66218cd59f` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaReadConfigFromPath, HdaUniversalBootChimeDxeEntry |
| `HdaReadConfigFromPath` | 781–841 | `1448e0b3576d` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaDebugInit, HdaUniversalBootChimeDxeEntry |
| `HdaOpenOrCreateDir` | 843–876 | `7e28feb43b9b` | Lido/indexado; análise manual pendente | NÃO validada individualmente; I/O / persistência / tratamento de erro | HdaOpenDebugLog, HdaUniversalBootChimeDxeEntry |
| `HdaOpenDebugLog` | 878–996 | `9d0c37782cc1` | Lido/indexado; análise manual pendente | NÃO validada individualmente; I/O / persistência / tratamento de erro | HdaDebugInit, HdaEnsureAppendLog, HdaDumpAllControllers, HdaUniversalBootChimeDxeEntry |
| `HdaAsciiStartsWithInsensitive` | 1000–1009 | `29cccc9cd22b` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaFindAlcIdInAscii, HdaBootArgIsEnabledToken, HdaUniversalBootChimeDxeEntry |
| `HdaBootArgTokenBoundary` | 1011–1013 | `ab183e8955ea` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaFindAlcIdInAscii, HdaBootArgIsEnabledToken, HdaFindCodecDumpRequestInAscii, HdaUniversalBootChimeDxeEntry |
| `HdaFindAlcIdInAscii` | 1015–1063 | `0b2bb82df3dd` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaReadAlcIdFromBootArgs, HdaUniversalBootChimeDxeEntry |
| `HdaBootArgIsEnabledToken` | 1067–1086 | `05c1f6685254` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaFindCodecDumpRequestInAscii, HdaUniversalBootChimeDxeEntry |
| `HdaFindCodecDumpRequestInAscii` | 1088–1109 | `fab6172bd44c` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaReadAlcIdFromBootArgs, HdaUniversalBootChimeDxeEntry |
| `HdaConvertUtf16BootArgsInPlace` | 1111–1128 | `03ec8892c2fe` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaReadAlcIdFromBootArgs, HdaUniversalBootChimeDxeEntry |
| `HdaReadAlcIdFromBootArgs` | 1130–1224 | `15e7c43e081c` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaDebugInit, HdaUniversalBootChimeDxeEntry |
| `HdaDeferredCodecDumpNotify` | 1233–1327 | `25b12b43624f` | Lido/indexado; análise manual pendente | NÃO validada individualmente; TPL / callback / lifecycle | HdaServiceDeferredCodecDumpIfSafe, HdaUniversalBootChimeDxeEntry |
| `HdaServiceDeferredCodecDumpIfSafe` | 1329–1340 | `e7d1bcff4568` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaUniversalBootChimeDxeEntry |
| `HdaUninstallBootScreenDNotify` | 1342–1404 | `234ae5c807c5` | Lido/indexado; análise manual pendente | NÃO validada individualmente; TPL / callback / lifecycle | HdaDeferredCodecDumpNotify, HdaInstallBootScreenDNotify, HdaUniversalBootChimeDxeEntry |
| `HdaInstallBootScreenDNotify` | 1406–1571 | `3192c587f5c5` | Lido/indexado; análise manual pendente | NÃO validada individualmente; TPL / callback / lifecycle | HdaUniversalBootChimeDxeEntry |
| `HdaCleanupAsyncPlayback` | 1575–1709 | `5c86a9df0d22` | Lido/indexado; análise manual pendente | NÃO validada individualmente; TPL / callback / lifecycle | HdaDeferredCodecDumpNotify, HdaExitBootServicesCleanup, HdaAsyncPlaybackTick, HdaStartFastBootPlayback (+1) |
| `HdaExitBootServicesCleanup` | 1711–1754 | `a0ec14c5524e` | Lido/indexado; análise manual pendente | NÃO validada individualmente; TPL / callback / lifecycle | HdaUniversalBootChimeDxeEntry |
| `HdaInstallExitBootServicesCleanup` | 1757–1788 | `c4625a8cc5c1` | Lido/indexado; análise manual pendente | NÃO validada individualmente; TPL / callback / lifecycle | HdaStartFastBootPlayback, HdaUniversalBootChimeDxeEntry |
| `HdaPollForF6` | 1790–1858 | `9cf82e27ed9d` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDebugInit, HdaUniversalBootChimeDxeEntry |
| `HdaDebugInit` | 1860–1932 | `47d2054d7ce0` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaUniversalBootChimeDxeEntry |
| `HdaDebugFinish` | 1934–1939 | `40e1aad5c410` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaUniversalBootChimeDxeEntry |
| `HdaReadPciInfo` | 1942–1961 | `a61160f574b4` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | PciIsHdaController, HdaTryController, HdaUniversalBootChimeDxeEntry |
| `HdaNormalizeExactSuccess` | 1963–1966 | `350799ae1f4f` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaPciFlushExact, HdaMemRead8, HdaMemRead16, HdaMemRead32 (+7) |
| `HdaPciFlushExact` | 1968–1971 | `40e552091e01` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaCleanupCorbRirbState, HdaCorbRirbCommand, HdaAllocatePlaybackBuffers, HdaRestoreStreamDescriptorState (+5) |
| `HdaMemRead8` | 1973–1978 | `6247f08b5101` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaRequireNoRunningStreamsBeforeBusMasterEnable, HdaRequireControllerResetQuiescent, HdaAbortImmediateCommand, HdaAcknowledgeImmediateResult (+20) |
| `HdaMemRead16` | 1980–1985 | `2de35795772c` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaRequireNoRunningStreamsBeforeBusMasterEnable, HdaRequireControllerResetQuiescent, HdaControllerEnsureReady, HdaAbortImmediateCommand (+17) |
| `HdaRequireNoRunningStreamsBeforeBusMasterEnable` | 1987–2023 | `a6415c0378fb` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaCorbRirbCommand, HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaMemRead32` | 2025–2030 | `d192590c236a` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaControllerReset, HdaControllerEnsureReady, HdaImmediateCommandRaw, HdaRequireCorbRirbSnapshotUnchanged (+9) |
| `HdaMemWrite8` | 2032–2037 | `ff20789a02a8` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaCleanupCorbRirbState, HdaCorbRirbCommand, HdaResetStreamDescriptor, HdaRestoreStreamDescriptorState (+4) |
| `HdaMemWrite16` | 2039–2044 | `5ac49d8a19b1` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaAbortImmediateCommand, HdaAcknowledgeImmediateResult, HdaImmediateCommandRaw, HdaResetCorbReadPointer (+6) |
| `HdaMemWrite32` | 2046–2051 | `05da70e5b762` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaControllerReset, HdaControllerEnsureReady, HdaImmediateCommandRaw, HdaCleanupCorbRirbState (+4) |
| `HdaRequireControllerResetQuiescent` | 2053–2100 | `d1557d423059` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaControllerReset, HdaUniversalBootChimeDxeEntry |
| `HdaStall` | 2102–2122 | `6001409bd633` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaTraceStage, HdaExitBootServicesCleanup, HdaPollForF6, HdaControllerReset (+15) |
| `HdaControllerReset` | 2124–2175 | `af0b570c5ad9` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaControllerEnsureReady, HdaCorbRirbCommand, HdaStopOutputStream, HdaUniversalBootChimeDxeEntry |
| `HdaControllerEnsureReady` | 2179–2224 | `3f5843755f23` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaDumpControllerFull, HdaTryController, HdaUniversalBootChimeDxeEntry |
| `HdaAbortImmediateCommand` | 2226–2285 | `91ef4793e8e7` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaImmediateCommandRaw, HdaUniversalBootChimeDxeEntry |
| `HdaAcknowledgeImmediateResult` | 2287–2309 | `39727c49f819` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaImmediateCommandRaw, HdaUniversalBootChimeDxeEntry |
| `HdaImmediateCommandRaw` | 2311–2419 | `8c775890f891` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaImmediateCommand, HdaUniversalBootChimeDxeEntry |
| `HdaCorbRirbStatePending` | 2421–2427 | `093e8f98be76` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaDischargeRetainedCorbRirbBusMasterRestore, HdaCleanupRetainedCorbRirb, HdaCorbRirbCommand, HdaUniversalBootChimeDxeEntry |
| `HdaDischargeRetainedCorbRirbBusMasterRestore` | 2429–2459 | `36768a3c827b` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaDumpControllerFull, HdaRestorePciAttributes, HdaUniversalBootChimeDxeEntry |
| `HdaRequireCorbRirbSnapshotUnchanged` | 2461–2552 | `04126b66da30` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaCorbRirbCommand, HdaUniversalBootChimeDxeEntry |
| `HdaVerifyTemporaryCorbRirbProgramming` | 2554–2636 | `f042b2b6b1f1` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaCorbRirbCommand, HdaUniversalBootChimeDxeEntry |
| `HdaVerifyCorbRirbDmaEnabled` | 2638–2665 | `dc4a0185fbe9` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaCorbRirbCommand, HdaUniversalBootChimeDxeEntry |
| `HdaResetCorbReadPointer` | 2667–2701 | `ef7590c7fa09` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaCleanupCorbRirbState, HdaCorbRirbCommand, HdaUniversalBootChimeDxeEntry |
| `HdaResetRirbWritePointer` | 2703–2719 | `ecc6418fd2ac` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaCleanupCorbRirbState, HdaCorbRirbCommand, HdaUniversalBootChimeDxeEntry |
| `HdaCleanupCorbRirbState` | 2721–2979 | `b43b5f9b05bb` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaCleanupRetainedCorbRirb, HdaCorbRirbCommand, HdaUniversalBootChimeDxeEntry |
| `HdaCleanupRetainedCorbRirb` | 2981–2993 | `a533ce236901` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaExitBootServicesCleanup, HdaTryController, HdaUniversalBootChimeDxeEntry |
| `HdaCorbRirbCommand` | 2995–3457 | `5d00d26fcf40` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaImmediateCommand, HdaUniversalBootChimeDxeEntry |
| `HdaImmediateCommand` | 3459–3486 | `8689652f5933` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaGetParamChecked, HdaSetVerb, HdaApplyExactProfileVerbs, HdaGetConnections (+32) |
| `HdaGetParamChecked` | 3489–3503 | `5cc103437162` | Lido + análise dirigida | Revisão estática; execução EFI pendente; Fluxo / limites / erros | HdaGetParam, HdaClassifySelectedRoute, HdaAddRouteCandidate, HdaGetEffectiveAmpCapsChecked (+21) |
| `HdaGetParam` | 3505–3509 | `9c604dc0c240` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaGetSubnodes, HdaGetConnections, HdaDumpNodeFull, HdaDumpCodecFull (+19) |
| `HdaSetVerb` | 3511–3520 | `783352b3f156` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaApplyAlcIdPrebootVerbs, HdaApplyExactProfileVerbs, HdaReadPinSense, HdaProgramSelectedInputAmp (+15) |
| `HdaWidgetType` | 3522–3524 | `11f2a3bd4555` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaPathSearch, HdaTryQuickPath, HdaFindPlayablePath (+17) |
| `HdaAmpUnityGainFromCaps` | 3527–3537 | `fc9552fdb249` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaAmpOutUnmuteUnityFromCaps, HdaAmpInUnmuteUnityFromCaps, HdaAmpVolumeGainFromCaps, HdaUniversalBootChimeDxeEntry |
| `HdaAmpOutUnmuteUnityFromCaps` | 3539–3541 | `c86c12c92208` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaProgramAmpSafeUnity, HdaUniversalBootChimeDxeEntry |
| `HdaAmpInUnmuteUnityFromCaps` | 3543–3554 | `2c22e6f1c9b6` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaProgramSelectedInputAmp, HdaProgramAmpSafeUnity, HdaUniversalBootChimeDxeEntry |
| `HdaFindAlcIdProfile` | 3644–3657 | `f55f17914867` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaResolveAlcIdProfile, HdaUniversalBootChimeDxeEntry |
| `HdaResolveAlcIdProfile` | 3659–3662 | `1db00052f032` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaTryQuickPath, HdaFindPlayablePath, HdaUniversalBootChimeDxeEntry |
| `HdaFindAlcIdConfigProfile` | 3664–3677 | `6262c37e2501` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaApplyAlcIdPrebootVerbs, HdaSnapshotConfigProfileState, HdaSnapshotCodecState, HdaUniversalBootChimeDxeEntry |
| `HdaConfigVerbIsRollbackSafe` | 3679–3738 | `80f440c7bfe1` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaConfigSpanIsRollbackSafe, HdaUniversalBootChimeDxeEntry |
| `HdaConfigSpanIsRollbackSafe` | 3740–3754 | `d5e2fe6478a7` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaApplyAlcIdPrebootVerbs, HdaApplyExactProfileVerbs, HdaSnapshotConfigProfileState, HdaSnapshotCodecState (+1) |
| `HdaApplyAlcIdPrebootVerbs` | 3756–3805 | `fad12258716b` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaApplyExactProfileVerbs` | 3807–3863 | `5ca01fc23066` | Lido + análise dirigida | Revisão estática; execução EFI pendente; I/O / persistência / tratamento de erro | HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaFindCodecPolicy` | 3865–3881 | `ca15015e091c` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaTryQuickPath, HdaFindPlayablePath, HdaUniversalBootChimeDxeEntry |
| `HdaDefaultDevice` | 3883–3885 | `9981b97d0e45` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaPinDefaultIsDigital, HdaDumpNodeFull, HdaPinPriority, HdaComputeOutputPinCtl (+1) |
| `HdaDefaultPortConnectivity` | 3887–3889 | `7a1958b20110` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaPinDefaultLooksConnected, HdaDumpNodeFull, HdaPinPriority, HdaUniversalBootChimeDxeEntry |
| `HdaDefaultAssociation` | 3891–3893 | `feed2a1611c3` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaPinPriority, HdaUniversalBootChimeDxeEntry |
| `HdaDefaultSequence` | 3895–3897 | `2d7ca4c7d062` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaPinPriority, HdaUniversalBootChimeDxeEntry |
| `HdaDefaultConnectionType` | 3899–3901 | `14f0295e07c3` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaPinDefaultIsDigital, HdaDumpNodeFull, HdaPinPriority, HdaUniversalBootChimeDxeEntry |
| `HdaDefaultLocation` | 3903–3905 | `bd2481ce39e9` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaPinPriority, HdaUniversalBootChimeDxeEntry |
| `HdaDefaultColor` | 3907–3909 | `55bca85e49b3` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaPinPriority, HdaUniversalBootChimeDxeEntry |
| `HdaPinDefaultIsDigital` | 3911–3918 | `03d47bfba0d6` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaPinPriority, HdaClassifySelectedRoute, HdaTryFullAlcIdRoute, HdaTryAlcIdProfilePath (+8) |
| `HdaPinDefaultLooksConnected` | 3920–3930 | `4e40ac7cc45c` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaPinPriority, HdaTryAlcIdProfilePath, HdaTryQuickPath, HdaAlcIdLayoutSafeFanout (+3) |
| `HdaGetSubnodes` | 3932–3957 | `324824fc3a38` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpCodecFull, HdaTryQuickPath, HdaFindPlayablePath, HdaCollectRouteCandidates (+7) |
| `HdaGetConnections` | 3959–4042 | `75687d5724b6` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpConnectionList, HdaPathSearch, HdaValidateFullRoute, HdaPathInputAmpsProgrammable (+7) |
| `HdaDumpWrite` | 4045–4055 | `0cc034c0c18c` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpConnectionList, HdaDumpNodeFull, HdaDumpCodecFull, HdaDumpControllerFull (+2) |
| `HdaDumpWidgetTypeName` | 4058–4071 | `a8f214450908` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaUniversalBootChimeDxeEntry |
| `HdaDumpDefaultDeviceName` | 4073–4092 | `05eac8d52b70` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaUniversalBootChimeDxeEntry |
| `HdaDumpConnTypeName` | 4094–4111 | `6fef3f1086df` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaUniversalBootChimeDxeEntry |
| `HdaDumpPortConnName` | 4113–4121 | `71729f3ad8c9` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaUniversalBootChimeDxeEntry |
| `HdaOpenLoadedImageRoot` | 4123–4150 | `39835b1b469a` | Lido/indexado; análise manual pendente | NÃO validada individualmente; I/O / persistência / tratamento de erro | HdaEnsureAppendLog, HdaDumpAllControllers, HdaUniversalBootChimeDxeEntry |
| `HdaDumpConnectionList` | 4155–4170 | `5eadf5b3b0aa` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpNodeFull, HdaUniversalBootChimeDxeEntry |
| `HdaEnsureAppendLog` | 4173–4196 | `7c706a933723` | Lido/indexado; análise manual pendente | NÃO validada individualmente; I/O / persistência / tratamento de erro | HdaDeferredCodecDumpNotify, HdaAsyncPlaybackTick, HdaUniversalBootChimeDxeEntry |
| `HdaCloseAppendLog` | 4198–4203 | `a80ea0e6e11c` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaDeferredCodecDumpNotify, HdaAsyncPlaybackTick, HdaUniversalBootChimeDxeEntry |
| `HdaDumpNodeFull` | 4205–4291 | `d7ac7e493c7c` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpCodecFull, HdaUniversalBootChimeDxeEntry |
| `HdaDumpCodecFull` | 4293–4370 | `3425cfa65253` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaDumpControllerFull, HdaUniversalBootChimeDxeEntry |
| `HdaDumpControllerFull` | 4372–4533 | `acc1775d6488` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpAllControllers, HdaUniversalBootChimeDxeEntry |
| `HdaPathSearch` | 4535–4621 | `d0baedf2e538` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaTryAlcIdProfilePath, HdaTryQuickPath, HdaFindPlayablePath, HdaCollectRouteCandidates (+4) |
| `HdaReadPinSense` | 4623–4647 | `9f72d9b431d6` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaPinPriority, HdaProgramDigitalOutputPath, HdaUniversalBootChimeDxeEntry |
| `HdaPinPriority` | 4649–4805 | `4d7e9a8405e9` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaTryFullAlcIdRoute, HdaFindPlayablePath, HdaCollectRouteCandidates, HdaUniversalBootChimeDxeEntry |
| `HdaClassifySelectedRoute` | 4815–4852 | `96e1148ed93c` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaTryFullAlcIdRoute, HdaTryAlcIdProfilePath, HdaTryQuickPath, HdaFindPlayablePath (+1) |
| `HdaValidateFullRoute` | 4854–4886 | `4e2328b71524` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaTryFullAlcIdRoute, HdaUniversalBootChimeDxeEntry |
| `HdaTryFullAlcIdRoute` | 4888–4961 | `b46e73fdbc86` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaTryAlcIdProfilePath, HdaUniversalBootChimeDxeEntry |
| `HdaTryAlcIdProfilePath` | 4965–5112 | `057fd675aa73` | Lido + análise dirigida | C extraído + execução host OK; EFI pendente; HDA / seleção / estado | HdaTryQuickPath, HdaFindPlayablePath, HdaUniversalBootChimeDxeEntry |
| `HdaTryQuickPath` | 5114–5252 | `c5307c3d49af` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaFindPlayablePath` | 5255–5403 | `d6c27298f0fc` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaRouteCandidateEquals` | 5406–5418 | `c53c3faf95b1` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaAddRouteCandidate, HdaUniversalBootChimeDxeEntry |
| `HdaAddRouteCandidate` | 5420–5479 | `23aba9049bc2` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaCollectRouteCandidates, HdaUniversalBootChimeDxeEntry |
| `HdaSortRouteCandidates` | 5481–5496 | `44d74d293a6e` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaCollectRouteCandidates, HdaUniversalBootChimeDxeEntry |
| `HdaCollectRouteCandidates` | 5498–5544 | `f67e5f75b3e0` | Lido + análise dirigida | C extraído + execução host OK; EFI pendente; HDA / seleção / estado | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaLoadRouteCandidate` | 5546–5573 | `2db445177ceb` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaGetEffectiveAmpCapsChecked` | 5576–5590 | `1cb511e8e364` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaProgramSelectedInputAmp, HdaProgramAmpSafeUnity, HdaProgramStoredHardwareVolume, HdaTryHardwareOutputVolume (+2) |
| `HdaPathInputAmpsProgrammable` | 5592–5634 | `0d1ca467e5e3` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaValidateFullRoute, HdaTryAlcIdProfilePath, HdaTryQuickPath, HdaFindPlayablePath (+5) |
| `HdaProgramSelectedInputAmp` | 5636–5663 | `cd5a8eaee44b` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaProgramConnectionPath, HdaUniversalBootChimeDxeEntry |
| `HdaProgramConnectionPath` | 5665–5722 | `81fb04bc1667` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaSelectConnectionForPath, HdaProgramAnalogOutputPath, HdaProgramDigitalOutputPath, HdaUniversalBootChimeDxeEntry |
| `HdaSelectConnectionForPath` | 5724–5728 | `c01b1ebfcbe8` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaProgramAmpSafeUnity` | 5730–5786 | `738ede4f9c33` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaProgramOutputDacStream, HdaProgramAnalogOutputPath, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaAmpVolumeGainFromCaps` | 5804–5843 | `c537dc8c0b9b` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaProgramStoredHardwareVolume, HdaTryHardwareOutputVolume, HdaTryHardwareInputVolume, HdaUniversalBootChimeDxeEntry |
| `HdaVerifyAmpGain` | 5845–5881 | `2352d72c6d12` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaProgramStoredHardwareVolume, HdaUniversalBootChimeDxeEntry |
| `HdaProgramStoredHardwareVolume` | 5883–5955 | `bb99d5332599` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaTryHardwareOutputVolume, HdaTryHardwareInputVolume, HdaProgramPlaybackVolume, HdaUniversalBootChimeDxeEntry |
| `HdaTryHardwareOutputVolume` | 5957–5979 | `3fcac5e446c1` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaProgramPlaybackVolume, HdaUniversalBootChimeDxeEntry |
| `HdaTryHardwareInputVolume` | 5981–6018 | `7280cee2b9bd` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaProgramPlaybackVolume, HdaUniversalBootChimeDxeEntry |
| `HdaProgramPlaybackVolume` | 6020–6057 | `727472631a65` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaU8ArrayContains` | 6059–6066 | `1edaf8a76ae1` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaCollectAudioOutputDacs, HdaBuildAlcIdDacOrder, HdaAlcIdLayoutSafeFanout, HdaEnableLegacyRealtekFanout (+1) |
| `HdaIsOutputPin` | 6068–6077 | `35b77fcfc0f8` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaValidateFullRoute, HdaTryAlcIdProfilePath, HdaTryQuickPath, HdaProgramOutputPin (+3) |
| `HdaSetPowerD0AndWait` | 6079–6096 | `387d52b28bb2` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaSetWidgetPowerD0, HdaProgramDigitalOutputPath, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaSetWidgetPowerD0` | 6098–6107 | `3c04743cbad5` | Lido + análise dirigida | Revisão estática; execução EFI pendente; Fluxo / limites / erros | HdaRestoreCodecState, HdaProgramOutputPin, HdaProgramOutputDacStream, HdaProgramAnalogOutputPath (+4) |
| `HdaFindConnectionIndexForNext` | 6109–6129 | `ea27eb7f088c` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaSnapshotOneNode, HdaSnapshotPathNodes, HdaUniversalBootChimeDxeEntry |
| `HdaSnapshotOneNode` | 6131–6318 | `c973c395d562` | Lido + análise dirigida | Revisão estática; execução EFI pendente; Fluxo / limites / erros | HdaSnapshotPathNodes, HdaCaptureProfileVerbState, HdaSnapshotCodecState, HdaUniversalBootChimeDxeEntry |
| `HdaFindNodeSnapshot` | 6320–6327 | `b6b726334d04` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaSnapshotPathNodes, HdaCaptureProfileVerbState, HdaUniversalBootChimeDxeEntry |
| `HdaCaptureSnapshotValue` | 6329–6347 | `e4c55c019c8f` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaCaptureProfileVerbState, HdaUniversalBootChimeDxeEntry |
| `HdaSnapshotPathNodes` | 6349–6419 | `09d231ae7ca2` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaSnapshotCodecState, HdaRealtekProgramPinDacProfile, HdaAlcIdLayoutSafeFanout, HdaEnableLegacyRealtekFanout (+1) |
| `HdaSnapshotWarmResetPower` | 6421–6461 | `f67c73d1db88` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaSnapshotCodecState, HdaUniversalBootChimeDxeEntry |
| `HdaCaptureProfileVerbState` | 6463–6597 | `b17acdbaa640` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaSnapshotConfigProfileState, HdaSnapshotCodecState, HdaUniversalBootChimeDxeEntry |
| `HdaGetCoefIndexSnapshot` | 6599–6618 | `79c336cda0d2` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaSnapshotCoefficientSpan, HdaUniversalBootChimeDxeEntry |
| `HdaSnapshotCoefficientValue` | 6620–6643 | `b3c159142ab8` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaSnapshotCoefficientSpan, HdaUniversalBootChimeDxeEntry |
| `HdaSnapshotCoefficientSpan` | 6645–6722 | `9ff6dce7a0f9` | Lido + análise dirigida | Revisão estática; execução EFI pendente; Fluxo / limites / erros | HdaSnapshotConfigProfileState, HdaSnapshotCodecState, HdaUniversalBootChimeDxeEntry |
| `HdaSnapshotConfigProfileState` | 6724–6759 | `1240f2a5e393` | Lido/indexado; análise manual pendente | NÃO validada individualmente; I/O / persistência / tratamento de erro | HdaSnapshotCodecState, HdaUniversalBootChimeDxeEntry |
| `HdaSnapshotCodecState` | 6761–6901 | `bbb5a57bd722` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaRestoreCodecState` | 6903–7142 | `4acfe9f1a614` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaRestoreCodecStateRetry, HdaUniversalBootChimeDxeEntry |
| `HdaRestoreCodecStateRetry` | 7144–7163 | `315b4bc7a9b8` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaCleanupAsyncPlayback, HdaReleasePreparedStreamAndBuffers, HdaAsyncPlaybackTick, HdaPlayChime (+1) |
| `HdaIsAudioOutputDac` | 7165–7194 | `8ad552db862b` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaValidateFullRoute, HdaTryAlcIdProfilePath, HdaTryQuickPath, HdaFindPlayablePath (+6) |
| `HdaSelectPcmFormat` | 7196–7253 | `e29eab06dfca` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaCollectAudioOutputDacs` | 7255–7281 | `b8c26f16175f` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaTryAlcIdProfilePath, HdaTryQuickPath, HdaCollectRouteCandidates, HdaAlcIdLayoutSafeFanout (+2) |
| `HdaBuildAlcIdDacOrder` | 7283–7314 | `5e0a48979687` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaTryAlcIdProfilePath, HdaAlcIdLayoutSafeFanout, HdaUniversalBootChimeDxeEntry |
| `HdaLegacyRealtekFanoutAllowed` | 7316–7322 | `02cb865c275b` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaEnableLegacyRealtekFanout, HdaUniversalBootChimeDxeEntry |
| `HdaRealtekFanoutReason` | 7324–7328 | `07001575a3bc` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaEnableLegacyRealtekFanout, HdaUniversalBootChimeDxeEntry |
| `HdaRealtekLegacyPinDriveAllowed` | 7330–7334 | `6867ec172f93` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaComputeOutputPinCtl, HdaUniversalBootChimeDxeEntry |
| `HdaComputeOutputPinCtl` | 7339–7375 | `da074b5adb99` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaProgramOutputPin, HdaUniversalBootChimeDxeEntry |
| `HdaLogSelectedPathState` | 7377–7422 | `40ec20e08e87` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaProgramOutputPin` | 7424–7522 | `d8d563ff213f` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaProgramAnalogOutputPath, HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaProgramOutputDacStream` | 7525–7555 | `adfc828dc1f1` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaProgramAnalogOutputPath, HdaProgramDigitalOutputPath, HdaPostStartCodecReapply, HdaUnmutePath (+1) |
| `HdaProgramAnalogOutputPath` | 7558–7600 | `94b871059aa7` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaRealtekProgramPinDacProfile, HdaAlcIdLayoutSafeFanout, HdaEnableLegacyRealtekFanout, HdaUniversalBootChimeDxeEntry |
| `HdaRealtekGpioAmpInit` | 7608–7616 | `24e6ea9f1a8e` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaPathContainsPinIntermediate` | 7633–7654 | `a700a9a5f246` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaValidateFullRoute, HdaTryAlcIdProfilePath, HdaCollectRouteCandidates, HdaRealtekProgramPinDacProfile (+3) |
| `HdaResetFanoutSelectorClaims` | 7656–7687 | `e35849b3d18e` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaPathsSelectorCompatible, HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaPathsSelectorCompatible` | 7689–7729 | `f772be5e9fcb` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaRealtekProgramPinDacProfile, HdaAlcIdLayoutSafeFanout, HdaEnableLegacyRealtekFanout, HdaUniversalBootChimeDxeEntry |
| `HdaCommitFanoutSelectorClaims` | 7731–7759 | `2e890c6a5aa1` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaRealtekProgramPinDacProfile, HdaAlcIdLayoutSafeFanout, HdaEnableLegacyRealtekFanout, HdaUniversalBootChimeDxeEntry |
| `HdaRealtekBuildDacPreferenceOrder` | 7761–7788 | `b21a6c45d5ec` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaRealtekAppleAlcComboInit, HdaEnableLegacyRealtekFanout, HdaUniversalBootChimeDxeEntry |
| `HdaRealtekProgramPinDacProfile` | 7791–7852 | `aa691e322502` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaRealtekAppleAlcComboInit, HdaUniversalBootChimeDxeEntry |
| `HdaRealtekCodecWarmReset` | 7854–7878 | `bd0bc8404e05` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaAlcIdLayoutSafeFanout` | 7880–8011 | `3a29ff8b84ce` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaRealtekAppleAlcComboInit` | 8013–8084 | `b3015a20c321` | Lido + análise dirigida | Revisão estática; execução EFI pendente; Fluxo / limites / erros | HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaEnableLegacyRealtekFanout` | 8086–8217 | `5da28db5a1d8` | Lido + análise dirigida | Guardas/ordem da regressão OK; EFI pendente; HDA / seleção / estado | HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaLogAlcIdFanoutState` | 8219–8239 | `fed56be19a2e` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaProgramHdmiInfoFrame` | 8241–8281 | `9ad250699b58` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaProgramDigitalOutputPath, HdaUniversalBootChimeDxeEntry |
| `HdaProgramDigitalOutputPath` | 8283–8350 | `306ce6066c05` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaPostStartCodecReapply, HdaUnmutePath, HdaUniversalBootChimeDxeEntry |
| `HdaPostStartCodecReapply` | 8352–8384 | `f41bb8d4df9c` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaUnmutePath` | 8387–8447 | `0ac96e16ce3b` | Lido + análise dirigida | Revisão estática; execução EFI pendente; HDA / seleção / estado | HdaTryAlcIdProfilePath, HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaCopyBootChime` | 8450–8479 | `c36ed2ebea67` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaAllocatePlaybackBuffers, HdaUniversalBootChimeDxeEntry |
| `HdaAllocatePlaybackBuffers` | 8481–8703 | `db2e2ba85a0b` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaFreePlaybackBuffers` | 8705–8798 | `4e329481ef9c` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaCleanupAsyncPlayback, HdaAllocatePlaybackBuffers, HdaFinalizePlaybackContext, HdaAsyncPlaybackTick (+1) |
| `HdaFinalizePlaybackContext` | 8800–8828 | `9f752ba31bbd` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaReleasePreparedStreamAndBuffers, HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaCaptureIdleStreamState` | 8830–8886 | `c8a661d29eb4` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaPrepareOutputStream, HdaUniversalBootChimeDxeEntry |
| `HdaRequireIdleStreamSnapshotUnchanged` | 8888–8936 | `ebbdc9fd7daa` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaPrepareOutputStream, HdaUniversalBootChimeDxeEntry |
| `HdaRequireStreamTagAvailable` | 8938–8982 | `bc6f2e11bfc2` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaPrepareOutputStream, HdaStartOutputStream, HdaUniversalBootChimeDxeEntry |
| `HdaResetStreamDescriptor` | 8984–9016 | `6dfcca8e0dd2` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaRestoreStreamDescriptorState, HdaPrepareOutputStream, HdaUniversalBootChimeDxeEntry |
| `HdaWriteStreamLviPreserve` | 9018–9031 | `f73ef4915342` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaRestoreStreamDescriptorState, HdaPrepareOutputStream, HdaUniversalBootChimeDxeEntry |
| `HdaWriteStreamFormatPreserve` | 9033–9047 | `052149709154` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaRestoreStreamDescriptorState, HdaPrepareOutputStream, HdaUniversalBootChimeDxeEntry |
| `HdaVerifyPreparedOutputStream` | 9049–9101 | `a7568b8ed3bd` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaPrepareOutputStream, HdaStartOutputStream, HdaUniversalBootChimeDxeEntry |
| `HdaVerifyRestoredStreamDescriptorState` | 9103–9165 | `16f9d87c69af` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaRestoreStreamDescriptorState, HdaUniversalBootChimeDxeEntry |
| `HdaRestoreStreamDescriptorState` | 9167–9216 | `55f1588c2590` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaStopOutputStream, HdaUniversalBootChimeDxeEntry |
| `HdaPrepareOutputStream` | 9218–9403 | `0f5b1c1413cb` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaMonitorPlaybackProgress` | 9406–9501 | `3ddeb9ed0a21` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaStartOutputStream` | 9503–9535 | `31cabf4bf20f` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaGetPciAttributesExact` | 9538–9551 | `54475d9d4e05` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaCorbRirbCommand, HdaDumpControllerFull, HdaVerifyPciAttributeBits, HdaApplyPciAttributeBits (+3) |
| `HdaVerifyPciAttributeBits` | 9553–9567 | `cd36ecce8645` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaCleanupCorbRirbState, HdaCorbRirbCommand, HdaDumpControllerFull, HdaApplyPciAttributeBits (+4) |
| `HdaApplyPciAttributeBits` | 9569–9617 | `693414be1b61` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaDumpControllerFull, HdaRestorePciAttributes, HdaUniversalBootChimeDxeEntry |
| `HdaRestorePciAttributes` | 9619–9654 | `03b9dcdcd3e2` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaCleanupAsyncPlayback, HdaAsyncPlaybackTick, HdaTryController, HdaUniversalBootChimeDxeEntry |
| `HdaRetainContextForCleanup` | 9657–9673 | `9d50996261af` | Lido + análise dirigida | Revisão estática; execução EFI pendente; TPL / callback / lifecycle | HdaFinalizePlaybackContext, HdaReleasePreparedStreamAndBuffers, HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaStopOutputStream` | 9675–9792 | `55e9022f90d7` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaCleanupAsyncPlayback, HdaReleasePreparedStreamAndBuffers, HdaAsyncPlaybackTick, HdaPlayChime (+1) |
| `HdaReleasePreparedStreamAndBuffers` | 9794–9812 | `d4d946fbf11d` | Lido + análise dirigida | Revisão estática; execução EFI pendente; DMA / hardware / ownership | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaAsyncPlaybackTick` | 9815–10001 | `da6f20a53362` | Lido + análise dirigida | Revisão estática; execução EFI pendente; TPL / callback / lifecycle | HdaUniversalBootChimeDxeEntry |
| `HdaStartFastBootPlayback` | 10003–10060 | `95b92a2552dd` | Lido + análise dirigida | Revisão estática; execução EFI pendente; TPL / callback / lifecycle | HdaPlayChime, HdaUniversalBootChimeDxeEntry |
| `HdaPlayChime` | 10062–10326 | `816c0ce7e69d` | Lido + análise dirigida | Revisão estática; execução EFI pendente; Fluxo / limites / erros | HdaTryController, HdaUniversalBootChimeDxeEntry |
| `PciIsHdaController` | 10328–10337 | `5c38d90ef0ec` | Lido/indexado; análise manual pendente | NÃO validada individualmente; DMA / hardware / ownership | HdaDumpAllControllers, HdaUniversalBootChimeDxeEntry |
| `HdaDumpAllControllers` | 10340–10451 | `7c2fb9390f85` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDumpAllControllersDynamic, HdaUniversalBootChimeDxeEntry |
| `HdaDumpAllControllersDynamic` | 10454–10480 | `0ae219b71e6a` | Lido/indexado; análise manual pendente | NÃO validada individualmente; Fluxo / limites / erros | HdaDeferredCodecDumpNotify, HdaUniversalBootChimeDxeEntry |
| `HdaDumpCodecBasic` | 10482–10520 | `2bf0e02a9c20` | Lido/indexado; análise manual pendente | NÃO validada individualmente; HDA / seleção / estado | HdaTryController, HdaUniversalBootChimeDxeEntry |
| `HdaTryController` | 10522–10739 | `9b37043ff4c4` | Lido + análise dirigida | Revisão estática; execução EFI pendente; Fluxo / limites / erros | HdaUniversalBootChimeDxeEntry |
| `HdaUniversalBootChimeDxeEntry` | 230–10894 | `e776a7c576df` | Lido + análise dirigida | Revisão estática; execução EFI pendente; Fluxo / limites / erros | — |

## Matriz de funções e métodos Python (produção e testes)

- Todas as definições Python foram indexadas pela AST; um `test_*` aprovado valida apenas sua asserção e condições concretas, não todas as ramificações de produção que ele menciona.

| Arquivo | Função/método | Linhas | Lido / analisado | Evidência |
|---|---|---|---|---|
| `Tests/test_audit_regressions.py` | `load_generator` | 27–33 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `plist_bytes` | 39–40 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `make_archive` | 43–75 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `exact_entry` | 78–90 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `extract_c_function` | 93–107 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `extract_c_function_definition` | 110–125 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_valid_generic_profile_is_generated` | 129–147 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_equivalent_mask_domains_with_conflicting_verbs_are_rejected` | 149–160 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_strict_subset_exact_profile_remains_valid` | 162–173 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_unsafe_resource_path_is_rejected` | 175–180 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exact_overlap_checks_skip_unrelated_layout_domains` | 182–242 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `counted_overlap` | 231–234 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_oversized_configdata_is_rejected_before_verb_materialization` | 244–262 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exact_profile_group_has_quadratic_work_safety_cap` | 264–281 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_duplicate_archive_members_are_rejected` | 283–293 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_malformed_plist_parser_errors_are_normalized` | 295–305 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_malformed_plist_fragment_parser_errors_are_normalized` | 307–310 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_malformed_optional_layout_keeps_existing_skip_semantics` | 312–327 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 331–333 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `array_body` | 335–340 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_profile_pair_arrays_match_c_capacity` | 342–352 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_generated_offsets_stay_inside_backing_arrays` | 354–380 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 384–386 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_timer_arm_failure_uses_central_cleanup_and_aborts_reuse` | 388–392 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_blocking_fallback_is_suppressed_after_ownership_transfer` | 394–399 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_central_cleanup_does_not_free_event_owned_buffers` | 401–410 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_repeated_callback_after_failed_close_avoids_stream_registers` | 412–417 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_callback_releases_buffers_only_after_event_close_success` | 419–427 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 431–435 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_retained_ownership_has_exit_boot_services_retry` | 437–448 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_rirb_device_addresses_require_128_byte_alignment` | 450–454 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_rirb_respect_gcap_64ok` | 456–460 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_unsolicited_rirb_responses_are_ignored` | 462–467 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_read_pointer_reset_is_handshaked` | 469–475 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_dispatch_waits_for_pci_io_and_guard_is_after_handle_discovery` | 477–481 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_plist_member_read_errors_are_normalized` | 483–492 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_atomic_output_failure_cleans_temporary_file` | 494–501 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 505–510 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_failed_corb_rirb_calls_never_publish_unowned_outputs` | 512–536 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_failed_playback_map_never_publishes_unowned_mapping` | 538–555 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_failed_console_protocol_lookup_cannot_publish_invalid_protocol` | 557–561 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_failed_locate_handle_buffer_is_never_freed` | 563–571 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_failed_log_open_cannot_publish_invalid_global_handle` | 573–579 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_directory_retry_clears_failed_open_output` | 581–591 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_loaded_image_root_publishes_handle_only_after_success` | 593–602 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_failed_single_shot_protocol_install_clears_output_handle` | 604–613 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_generator_groups_exact_overlap_checks_by_codec_layout` | 615–633 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_generator_rejects_oversized_archive_before_zip_parsing` | 635–641 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_generator_rejects_total_uncompressed_archive_limit` | 643–654 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_generator_plist_depth_and_object_limits_are_bounded_iteratively` | 656–669 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_fixed_volume_copy_has_no_dead_gain_assignment` | 671–674 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_audio_dma_device_range_cannot_wrap_before_bdl_split` | 676–684 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_bdl_has_at_least_two_entries_and_lvi_matches` | 686–693 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_fastboot_requires_exit_boot_services_cleanup_before_transfer` | 695–700 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_terminal_cleanup_minimizes_external_work_at_exit_boot_services` | 702–710 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exit_boot_services_never_reenables_ring_dma` | 712–716 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_largest_supported_corb_rirb_ring_is_preferred` | 718–724 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_rirb_overrun_is_detected_and_not_treated_as_timeout` | 726–733 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_configdata_write_failure_is_propagated` | 735–739 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_coefficient_index_uses_4bit_verb_16bit_payload` | 741–749 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_generated_profiles_fit_coefficient_snapshot_capacity` | 751–780 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_coefficient_snapshot_restores_reused_index_snapshot` | 782–787 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exact_profile_is_snapshotted_or_skipped_before_application` | 789–795 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exact_profile_code_does_not_reference_nonexistent_name_field` | 797–799 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_general_hardware_access_requires_exit_boot_services_cleanup_hook` | 801–813 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_controller_iteration_stops_on_any_retained_hardware_ownership` | 815–822 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_deferred_codec_dump_cannot_reenter_controller_critical_section` | 824–829 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_controller_critical_section_services_deferred_dump_after_owner_returns` | 831–842 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_interactive_dump_notify_is_installed_after_config_requested_dump` | 844–852 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_build_runs_repeated_source_regressions_before_edk2_setup` | 854–862 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_boot_args_success_size_cannot_exceed_allocated_capacity` | 864–875 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stream_descriptor_ownership_is_published_only_before_first_mutation` | 877–888 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_hda_atapi_connection_type_matches_spec_encoding` | 890–892 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_critical_create_event_success_requires_nonnull_handle` | 894–911 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_release_metadata_and_diagnostics_are_consistent` | 913–922 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_release_tree_has_single_consolidated_auxiliary_log` | 924–930 | Inspeção direta, ajuste mínimo e prova positiva/negativa | 1 patch R38 permitido, outros .patch/.diff/.sha256/.log reprovados |
| `Tests/test_audit_regressions.py` | `test_release_tree_has_no_cache_or_orphan_temp_artifacts` | 931–947 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_active_source_uses_requested_copyright_and_contact` | 949–961 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_helper_cleans_interrupted_temp_file` | 963–971 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_helper_uses_single_deterministic_backup` | 973–980 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_build_tooling_uses_one_log_and_pinned_edk2_ref` | 982–990 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_requires_pci_attribute_snapshot_before_mmio_enable` | 992–1001 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_restore_failure_is_not_masked` | 1003–1008 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_rirb_successful_allocatebuffer_requires_nonnull_host` | 1010–1023 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_rirb_cleanup_error_overrides_transport_error` | 1025–1030 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_propagates_controller_errors_and_stops_on_retained_rings` | 1032–1039 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_rirb_dma_ranges_cannot_wrap_physical_address_space` | 1042–1052 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_bdl_dma_range_cannot_wrap_physical_address_space` | 1054–1063 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_statests_mmio_failure_is_not_misclassified_as_no_codecs` | 1065–1074 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_32bit_dma_controllers_validate_full_mapped_ranges_not_only_bases` | 1077–1086 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_preserves_bus_master_containment_for_retained_rings` | 1089–1096 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_stops_same_controller_when_ring_ownership_is_retained` | 1098–1104 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_requested_codec_dump_never_enters_playback_with_retained_ring_ownership` | 1106–1116 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_preboot_key_poll_has_finite_input_drain_budget` | 1118–1124 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_rirb_cleanup_reverifies_dma_disabled_before_freeing_resources` | 1126–1136 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_append_log_has_hard_size_cap_and_recreates_single_file` | 1138–1146 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_file_warning_statuses_are_not_accepted_as_clean_log_io` | 1148–1155 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_config_file_warnings_are_rejected_before_parsing` | 1157–1164 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_debug_init_requires_exact_openvolume_success_and_closes_ambiguous_root` | 1167–1175 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_single_shot_protocol_guard_requires_clean_install_and_valid_handle` | 1177–1186 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_deferred_codec_dump_failure_remains_retryable` | 1188–1197 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_pci_attribute_reads_require_exact_success_and_read_back_mutated_bits` | 1200–1207 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_playback_pci_memory_enable_is_verified_before_mmio` | 1209–1220 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_pci_restore_is_verified_before_ownership_is_cleared` | 1222–1235 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_dma_containment_requires_bus_master_read_back` | 1237–1254 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_event_and_key_notify_ownership_requires_exact_success` | 1256–1276 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_protocol_and_handle_enumeration_reject_warning_statuses` | 1278–1294 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_warning_status_cannot_be_lost_by_efi_error_macro` | 1296–1304 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_mmio_and_location_outputs_require_exact_success` | 1306–1312 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_dma_acquisition_rejects_warning_status_before_publishing_ownership` | 1314–1333 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_dma_release_requires_exact_success_before_clearing_ownership` | 1335–1356 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_pci_flush_warning_is_normalized_before_hardware_progress` | 1358–1364 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_boot_args_requires_clean_success_before_text_scan` | 1366–1372 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 1376–1379 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_failed_create_event_output_is_never_consumed` | 1381–1393 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_failed_file_open_outputs_are_never_closed` | 1395–1408 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_file_close_helper_is_terminal` | 1410–1415 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_file_delete_is_terminal_even_on_delete_warning` | 1417–1427 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_close_event_helper_is_terminal` | 1429–1434 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_pci_unmap_is_never_retried_after_status` | 1436–1445 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_unregister_invalid_parameter_is_terminal_and_undocumented_status_quarantines` | 1447–1451 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exit_boot_services_callback_has_no_forbidden_boot_service_release_calls` | 1453–1471 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stall_is_centralized_and_boot_services_safe_during_exit` | 1473–1478 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_keyboard_reads_require_exact_success` | 1480–1482 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_gettime_output_is_used_only_on_exact_success` | 1484–1486 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_controller_dump_root_handle_uses_terminal_close_helper` | 1488–1492 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_build_stages_project_outside_edk2_workspace` | 1494–1499 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_build_restores_temporary_basetools_patch` | 1501–1505 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_build_failure_removes_partial_release_outputs_but_keeps_master_log` | 1507–1513 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r16_version_is_consistent_in_active_metadata` | 1515–1522 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 1526–1529 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_notification_events_use_callback_tpl_not_application_tpl` | 1531–1539 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_playback_dma_uses_bus_master_read_not_common_buffer` | 1541–1549 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_successful_playback_map_ownership_is_independent_of_mapping_token_value` | 1551–1561 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exit_boot_services_does_not_replay_codec_or_pci_snapshot` | 1563–1570 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exit_boot_services_stream_stop_returns_before_descriptor_restore` | 1572–1578 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exit_boot_services_ring_cleanup_does_not_replay_register_snapshot` | 1580–1587 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r16_documentation_is_authoritative_and_marks_superseded_rules` | 1589–1601 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r16_version_is_consistent_in_all_active_metadata` | 1603–1616 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 1621–1625 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_terminal_handoff_uses_before_exit_boot_services_group` | 1627–1635 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_before_exit_handoff_remains_allocation_and_delayed_work_free` | 1637–1647 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_single_shot_fails_closed_when_no_guard_can_be_armed` | 1649–1657 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r16_metadata_is_exact_and_prior_round_history_is_retained` | 1659–1668 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 1672–1674 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_existing_directory_open_uses_zero_attributes_without_create` | 1676–1685 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_key_dump_worker_is_deferred_to_next_timer_tick` | 1687–1693 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_successful_deferred_dump_releases_timer_event_in_worker` | 1695–1703 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r17_metadata_and_history_are_consistent` | 1705–1715 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 1719–1721 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_pci_attribute_rollback_never_replaces_the_entire_mask` | 1723–1733 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_fallback_owns_temporary_bus_master_enable` | 1735–1753 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_failed_fallback_bus_master_enable_still_enters_cleanup` | 1755–1762 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_playback_bus_master_is_delayed_until_final_pre_run_boundary` | 1764–1778 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_does_not_pre_enable_bus_master` | 1780–1787 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r18_metadata_and_history_are_consistent` | 1789–1804 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 1808–1810 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_external_pci_restore_can_discharge_fallback_bus_master_ownership` | 1812–1824 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_restore_manages_fallback_owned_bus_master_bit` | 1826–1835 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_bus_master_enable_is_refused_while_any_hda_stream_is_running` | 1837–1843 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_playback_and_corb_check_stream_quiescence_before_enabling_bus_master` | 1845–1854 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r19_metadata_and_all_prior_round_history_are_retained` | 1856–1872 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 1877–1879 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_fastboot_callback_cannot_preempt_entry_owned_log_state` | 1881–1893 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_key_request_stays_synchronous_while_entry_is_active` | 1895–1909 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_key_callback_reentrancy_rearms_timer_until_callback_unwinds` | 1911–1925 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r20_metadata_and_history_are_consistent` | 1927–1943 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 1947–1950 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_unsupported_common_ring_size_runs_cleanup_after_fallback_bme_enable` | 1952–1958 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_bus_master_containment_failure_has_error_priority` | 1960–1966 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r21_metadata_and_history_are_consistent` | 1968–1982 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 1986–1989 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_key_notify_exclusion_is_nesting_safe` | 1991–2003 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r22_metadata_and_history_are_consistent` | 2005–2019 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 2023–2026 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_success_without_key_notify_handle_is_permanently_contained` | 2028–2042 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_upper_unknown_registration_is_gated_before_known_lower_cleanup` | 2044–2056 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_registration_in_progress_blocks_reentrant_callback_and_worker` | 2058–2081 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_unknown_registration_discards_any_reentrant_pending_request` | 2083–2094 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_deferred_worker_honors_terminal_notify_containment_before_rearm` | 2096–2106 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_helper_enables_release_debug_override_transactionally` | 2108–2161 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_helper_preserves_first_backups_across_repeated_runs` | 2163–2198 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_helper_failure_never_publishes_partial_debug_profile` | 2200–2248 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_helper_does_not_invent_release_profile_when_missing` | 2250–2275 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r23_metadata_and_history_are_consistent` | 2277–2291 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 2295–2298 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_registration_barrier_is_released_only_after_ownership_decision` | 2300–2326 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_notify_ownership_mutation_barrier_covers_unregister_transaction` | 2328–2341 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_notify_mutation_barrier_blocks_key_and_deferred_worker_side_effects` | 2343–2358 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_primary_compile_command_is_present_executable_and_transactional` | 2360–2372 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r24_metadata_and_history_are_consistent` | 2374–2388 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 2392–2395 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_undocumented_register_key_notify_status_is_terminally_contained` | 2397–2431 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_default_edk2_toolchain_is_pinned_to_official_stable_commit` | 2433–2455 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r25_metadata_and_history_are_consistent` | 2457–2471 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 2475–2478 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_pinned_edk2_workspace_requires_verifiable_git_provenance` | 2480–2493 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_unpinned_custom_non_git_workspace_keeps_explicit_compatibility_path` | 2495–2504 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r26_metadata_and_history_are_consistent` | 2506–2526 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 2530–2533 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_controller_reset_requires_idle_rings_and_streams_before_crst_assert` | 2535–2552 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_full_controller_reset_waits_521us_after_observing_crst_one` | 2554–2563 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_immediate_command_timeout_aborts_owned_icb_and_clears_racing_irv` | 2565–2577 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_low_latency_already_running_controller_obeys_codec_discovery_settle` | 2579–2589 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_immediate_and_corb_transports_are_mutually_exclusive` | 2591–2617 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_owned_immediate_command_is_aborted_after_response_poll_read_error` | 2619–2629 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_immediate_command_preserves_ambiguous_entry_state` | 2631–2641 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_owned_immediate_response_is_retired_even_if_response_read_fails` | 2643–2661 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_fallback_never_clears_foreign_w1c_status_on_entry` | 2663–2674 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_snapshot_is_revalidated_before_first_register_mutation` | 2676–2695 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_rirb_rsvdp_fields_are_preserved_on_temporary_writes` | 2697–2723 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_temporary_ring_programming_and_dma_enable_are_read_back_before_dispatch` | 2725–2748 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_corb_memory_error_is_detected_and_reset_only_after_cleanup` | 2750–2767 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_idle_stream_descriptor_is_snapshotted_and_revalidated_before_reset` | 2769–2785 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_bidirectional_output_sets_dir_immediately_after_stream_reset` | 2787–2796 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stream_lvi_and_format_preserve_reserved_fields` | 2798–2806 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stream_programming_is_read_back_before_run_and_run_is_confirmed` | 2808–2822 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stopped_stream_is_reset_before_cbl_restore_and_prior_state_is_restored` | 2824–2838 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_all_nonzero_descriptor_tags_are_reserved_even_when_run_is_clear` | 2840–2847 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stream_candidate_rejects_reset_or_pending_status` | 2849–2853 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stream_tag_is_revalidated_immediately_before_descriptor_ownership_and_run` | 2855–2869 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r28_metadata_and_history_are_consistent` | 2871–2895 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 2899–2902 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_32bit_only_controller_never_writes_read_only_ring_upper_bases` | 2904–2919 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_32bit_only_controller_never_writes_read_only_stream_bdl_upper_base` | 2921–2934 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stream_tag_scan_rejects_concurrent_descriptor_mutation` | 2936–2942 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stream_restore_is_read_back_before_descriptor_ownership_is_released` | 2944–2965 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 2970–2973 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_start_revalidates_complete_descriptor_after_foreign_tag_scan` | 2975–2983 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_local_descriptor_restore_failure_never_escalates_to_global_crst` | 2985–2994 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_cleanup_degradation_keeps_real_status_without_forcing_replay` | 2996–3004 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_stream_descriptor_kind_is_owned_and_cleared_with_descriptor` | 3006–3011 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_async_cleanup_retains_real_cleanup_status_for_retry` | 3013–3017 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_generic_uefi_warning_code_is_not_misused_for_mmio_cleanup` | 3019–3021 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_fastboot_surfaces_stream_restore_degradation_before_context_release` | 3023–3030 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 3035–3038 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_packaging_uses_the_same_path_resolved_zip_tools_as_preflight` | 3040–3046 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r30_release_metadata_is_synchronized_and_history_is_preserved` | 3048–3069 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 3073–3076 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_source_hygiene_is_gated_before_python_test_imports` | 3078–3089 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_append_log_recreates_file_at_the_exact_size_limit` | 3091–3094 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r31_release_metadata_is_synchronized_and_r30_history_is_preserved` | 3096–3116 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 3120–3124 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_connection_list_rejects_sequential_range_entries` | 3126–3130 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_command_transports_reject_unsupported_indirect_nids` | 3132–3138 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_generator_rejects_indirect_configdata_nid_encoding` | 3140–3151 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_applealc_profile_fallback_skips_pin_when_configdefault_read_fails` | 3153–3158 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_controller_dump_propagates_mandatory_register_read_failures` | 3160–3167 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_volume_knob_widget_type_is_defined_and_named` | 3169–3172 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_widget_indexed_state_matches_direct_nid_domain` | 3174–3180 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_selected_route_classification_fails_closed_on_config_read_error` | 3182–3193 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_route_candidate_reuses_primary_classification_and_fallback_fails_closed` | 3196–3204 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_output_pin_programming_propagates_configdefault_read_failure` | 3206–3212 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_realtek_profile_fails_closed_when_configdefault_read_fails` | 3214–3216 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_legacy_realtek_fanout_skips_pins_when_configdefault_read_fails` | 3218–3221 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_full_applealc_route_does_not_bypass_digital_policy_on_config_read_error` | 3224–3227 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_path_search_validates_direct_nids_before_indexing_reduced_arrays` | 3230–3235 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_selector_claim_tables_validate_nids_before_128_entry_indexing` | 3238–3242 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_configdata_runtime_rejects_indirect_nids_before_snapshot_indexing` | 3245–3251 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r32_release_metadata_is_synchronized_and_r31_history_is_preserved` | 3254–3274 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 3278–3280 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_r33_release_metadata_is_synchronized_and_r32_history_is_preserved` | 3282–3304 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_quick_path_nonforced_configdefault_read_fails_closed` | 3306–3314 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_alcid_safe_fanout_configdefault_read_fails_closed` | 3316–3322 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_realtek_applealc_combo_configdefault_read_fails_closed` | 3324–3330 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_reads_subsystem_id_from_function_group_not_root` | 3333–3342 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_subsystem_id_is_limited_to_spec_function_group_types` | 3344–3350 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_codec_dump_propagates_mandatory_identity_read_failures` | 3352–3364 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_embedded_efi_validator_accepts_boot_service_driver_and_rejects_wrong_subsystem` | 3366–3412 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_build_validates_pe32plus_x64_efi_driver_before_copying_outputs` | 3414–3425 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 3429–3433 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `_validator_script` | 3435–3441 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `_minimal_efi_image` | 3443–3472 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `_run_validator` | 3474–3482 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_exact_profile_requires_successfully_read_revision_and_subsystem_identity` | 3484–3507 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_exact_profile_snapshot_uses_same_identity_validity_gate` | 3509–3518 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_pin_priority_fails_closed_before_parsing_or_pin_sense_when_config_default_read_fails` | 3520–3531 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_release_validator_rejects_zero_sections_and_out_of_file_section_raw_data` | 3533–3548 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_release_validator_requires_entry_point_in_section_and_directory_count_to_fit_optional_header` | 3550–3563 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_release_validator_walks_aligned_win_certificate_entries` | 3565–3586 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_parameter_transport_failures_cannot_masquerade_as_zero_capabilities_in_dac_selection` | 3589–3598 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_mutating_path_programming_propagates_required_capability_read_errors` | 3600–3612 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_snapshot_does_not_classify_failed_widget_caps_as_audio_output` | 3614–3620 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_fanout_selector_safety_fails_closed_when_widget_caps_are_unreadable` | 3622–3638 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_amp_capability_transport_errors_are_propagated_before_gain_programming` | 3640–3654 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_route_classification_rejects_unreadable_pin_capabilities` | 3656–3663 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_snapshot_node_is_published_only_after_complete_capture_and_pin_caps_are_known` | 3665–3675 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_warm_reset_snapshot_aborts_on_unreadable_widget_caps` | 3677–3682 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_transactional_snapshot_never_skips_required_eapd_or_input_amp_state_on_read_failure` | 3684–3694 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 3698–3700 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `_validator_script` | 3702–3708 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `_minimal_efi_image` | 3710–3738 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `_run_validator` | 3740–3748 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_release_validator_rejects_certificate_table_overlapping_section_raw_data` | 3750–3766 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_release_validator_rejects_section_raw_data_overlapping_headers_or_another_section` | 3769–3792 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_release_validator_requires_executable_image_characteristic` | 3795–3803 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_release_validator_rejects_invalid_file_and_section_alignment` | 3805–3820 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_fallback_candidate_classification_rejects_unreadable_pin_capabilities` | 3822–3834 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_input_amp_route_prefilter_fails_closed_when_widget_caps_are_unreadable` | 3836–3843 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_selected_route_classification_enforces_digital_policy_from_live_pin_caps` | 3847–3859 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_analog_path_programming_refuses_live_digital_pin_before_first_mutation` | 3862–3876 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 3880–3882 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `_validator_script` | 3884–3890 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_release_validator_accepts_valid_subpage_edk2_xcode5_alignment` | 3892–3932 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_release_validator_rejects_subpage_alignment_when_file_offset_differs_from_rva` | 3934–3973 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r37_release_metadata_is_synchronized_and_r36_history_is_preserved` | 3975–3989 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `setUpClass` | 3995–3997 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tests/test_audit_regressions.py` | `test_build_scrubs_transient_macos_metadata_before_hygiene_and_each_regression_round` | 3999–4014 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_hygiene_gate_still_rejects_macos_metadata_if_cleanup_does_not_remove_it` | 4016–4021 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_audit_regressions.py` | `test_r36_release_metadata_is_synchronized_and_r35_history_is_preserved` | 4023–4037 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_forcepin_runtime.py` | `definition` | 18–32 | Lido/análise dirigida | suporte de compilação host executado |
| `Tests/test_forcepin_runtime.py` | `setUpClass` | 95–100 | Lido/análise dirigida | suporte de compilação host executado |
| `Tests/test_forcepin_runtime.py` | `compile_run` | 102–110 | Lido/análise dirigida | suporte de compilação host executado |
| `Tests/test_forcepin_runtime.py` | `test_applealc_runtime_path_does_not_override_forced_pin` | 112–130 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_forcepin_runtime.py` | `test_route_fallback_does_not_override_forced_pin` | 132–156 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tests/test_forcepin_runtime.py` | `test_legacy_fanout_captures_all_touched_routes_before_programming` | 159–167 | Lido/indexado; executado | unittest OK; não equivale a inspeção manual de implementação |
| `Tools/generate_applealc_profiles.py` | `validate_plist_tree` | 43–57 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `parse_plist_bytes` | 77–82 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `load_plist_fragment` | 85–96 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `load_plist` | 99–102 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `as_int` | 104–117 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `leaf_node_chains` | 120–140 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `c_string` | 142–145 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `resource_member` | 148–158 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `read_plist_member` | 161–175 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `resource_root` | 178–189 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `pinconfigs_member` | 192–199 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `config_name` | 202–207 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `masked_constraints_overlap` | 210–213 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `exact_profile_is_subset` | 216–227 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `exact_profile_specificity` | 230–233 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `generate` | 236–706 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `verb_span` | 633–644 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `write_output_atomic` | 709–737 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |
| `Tools/generate_applealc_profiles.py` | `main` | 740–760 | Lido/indexado; revisão manual por função pendente | AST OK; execução isolada não comprovada |

## Matriz de funções shell do pipeline e utilitários

- As faixas do shell delimitam da definição até a próxima definição (inclusive intervenientes/heredocs); são aproximações de localização, não um parser de controle de fluxo. `bash -n` passou em todos os três arquivos.

| Arquivo | Função | Faixa aproximada | Análise / validação |
|---|---|---|---|
| `Build-HDAUniversalBootChimeDxe.command` | `log` | 48–48 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `warn` | 49–49 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `fail` | 50–51 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `restore_basetools_wrapper` | 52–62 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `cleanup_build_transaction` | 63–87 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `lowercase` | 88–91 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `cpu_count` | 92–107 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `require_command` | 108–112 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `clean_transient_source_metadata` | 113–131 | leitura estrutural; Executada até gate EDK2 |
| `Build-HDAUniversalBootChimeDxe.command` | `verify_source_tree_hygiene` | 132–146 | leitura estrutural; Executada até gate EDK2 |
| `Build-HDAUniversalBootChimeDxe.command` | `preflight_tools` | 147–165 | leitura estrutural; Executada até gate EDK2 |
| `Build-HDAUniversalBootChimeDxe.command` | `run_source_regressions` | 166–198 | leitura estrutural; Executada até gate EDK2 |
| `Build-HDAUniversalBootChimeDxe.command` | `ensure_basetools_c_binaries` | 199–212 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `patch_basetools_shell_wrappers` | 213–246 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `find_edk2_workspace` | 247–269 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `bootstrap_edk2_if_needed` | 270–307 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `verify_edk2_ref` | 308–334 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `prepare_edk2` | 335–384 | leitura estrutural; Executada até gate EDK2 |
| `Build-HDAUniversalBootChimeDxe.command` | `find_unique_file` | 385–406 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `stage_package_for_build` | 407–431 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `resolve_edk2_library_paths` | 432–539 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `select_toolchain` | 540–551 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `files_are_identical` | 552–569 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `find_output` | 570–625 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `validate_efi_image` | 626–811 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Build-HDAUniversalBootChimeDxe.command` | `build_one` | 812–900 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Setup-EDK2.command` | `fail` | 13–13 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Setup-EDK2.command` | `log` | 14–14 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Setup-EDK2.command` | `cpu_count` | 15–85 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Enable-HDAUCodecDump-NextBoot.command` | `is_boot_efi_root` | 14–18 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Enable-HDAUCodecDump-NextBoot.command` | `has_installed_driver` | 19–24 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Enable-HDAUCodecDump-NextBoot.command` | `find_efi_root` | 25–108 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Enable-HDAUCodecDump-NextBoot.command` | `cleanup_tmp` | 109–125 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Enable-HDAUCodecDump-NextBoot.command` | `set_key_file` | 126–154 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |
| `Enable-HDAUCodecDump-NextBoot.command` | `set_key` | 155–243 | leitura estrutural; Somente sintaxe; ramo/ambiente específico não comprovado |

## Pendências bloqueantes e status de Release

**RELEASE: NÃO COMPROVADO / PENDENTE.** O ZIP final é a entrega do **source atualizado**, não contém `.efi` que alegue ter sido compilado agora.

1. Preparar um checkout EDK2 verificável no commit fixado, compilar **DEBUG e RELEASE** em ambiente compatível e validar o PE32+ X64/EFI Boot Service Driver produzido.
2. Testar hardware HDA real, inclusive `ForcePin`, `RouteFallback`, `FullPathMap` e fanout legado habilitado com `TransactionalRestore=1`, medindo se todos os widgets extra-programados são restaurados antes de `ExitBootServices`.
3. Completar inspeção sem amostragem das funções ainda marcadas como não analisadas individualmente na matriz, especialmente interleavings TPL/DMA, resource leaks e fallbacks de controlador, e adicionar casos de fault injection sob as APIs UEFI reais.
4. Rodar estresse prolongado no alvo, cold/warm boots repetidos, verificação de estados pré/pós com codec dump, DMA boundaries, áudio/latência e warnings reais nas duas configurações.
5. Só depois dos itens anteriores poderá ser emitida afirmação de build/hardware/Release validado; não extrapolar os testes de host.

## Checkpoint persistente para retomada

- **Base autoritativa disponível:** ZIP SHA-256 `7149cc93193fe7cffc80262b493098e9de52940d8b048c66e0aa272f7b91f20f` (não há Git HEAD no anexo).
- **Source após correção:** `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` SHA-256 `2537228ca89640673f9b349bf170774b79ea3fd9f9c8a76ad4895b9e8966f96e`.
- **Teste de regressão novo:** `Tests/test_forcepin_runtime.py` SHA-256 `00d77fb92e43e0b97e50ad7eeb5406613421ffa3dc0aad1a22cfbef94cba16fb`.
- **Patch consolidado:** `Audit/diffs/R37-to-R38.patch` SHA-256 `82b9d76f95960e2570d39145642eafbede21766fcc08a987883f6740cca52a1d`; aplicação em árvore-base R37 limpa verificada com `git apply --check` e comparação byte por byte.
- **Últimas verificações concluídas:** correção de um teste da árvore R38 reproduzida antes/depois; negativos com arquivos `.patch/.diff/.sha256/.log` extras; 270/270 em Python normal; cinco rodadas completas no wrapper de build (270/270 em cada), etapa EDK2 ausente; sintaxe shell/AST e aplicação fiel de patch. A interrupção histórica da rodada R38 anterior permanece registrada e não é contabilizada como rodada concluída.
- **Próximo ponto exato:** obter EDK2 fixado, executar `AUTO_FETCH_EDK2=0 SOURCE_TEST_ROUNDS=5 ./Build-HDAUniversalBootChimeDxe.command` em macOS com XCODE5; preservar log real e EFI DEBUG/RELEASE; se aprovado, fazer ensaio HDA físico e então revisar funções marcadas pendentes nesta matriz.
- **Critério de mudança:** não alterar arquitetura, áudio PCM, AppleALC, versões, configuração ou aparência sem defeito comprovado e teste reproduzível. Consolidar alterações futuras neste relatório e no patch existente.


## Adendo R38 — correção do gate de higiene após log do macOS (19/10/2026)

**Evidência recebida:** terminal do macOS executou 270 testes, mas o gate interrompeu a primeira rodada porque `test_release_tree_has_single_consolidated_auxiliary_log` proibia qualquer `.patch`, enquanto o próprio ZIP R38 inclui o patch consolidado requerido `Audit/diffs/R37-to-R38.patch`. Falha na assertiva original da linha 929, sem indício de falha no código do driver nesse teste.

**Mudança mínima comprovada:** `Tests/test_audit_regressions.py`, função `test_release_tree_has_single_consolidated_auxiliary_log` (linhas 924–930). A assertiva passa a permitir apenas `Audit/diffs/R37-to-R38.patch` como arquivo auxiliar não-log. Continua exigindo exatamente `Release/Logs/BUILD-COMPILE.log` como log único e rejeita qualquer outro `.diff`, `.patch`, `.sha256` ou `.log` no projeto. As permissões executáveis já presentes no R37 foram restauradas em `Tests/test_audit_regressions.py` e `Tools/generate_applealc_profiles.py`, sem mudanças de conteúdo no gerador. O arquivo C HDA e os arquivos PCM permanecem *byte a byte idênticos* ao R38 anterior.

**Antes/depois e testes adversariais:** executado o teste original antes da alteração: falhou pelo `R37-to-R38.patch`. Depois: passou. Inseridos isoladamente no diretório `Audit/diffs` arquivos temporários indevidos `extra.patch`, `extra.diff`, `extra.sha256` e `extra.log`: em todos os quatro cenários o mesmo teste falhou, como esperado; os arquivos temporários foram removidos e o teste passou novamente. A suíte completa executou **270/270** testes em execução independente.

**Gate integrado:** executado `Build-HDAUniversalBootChimeDxe.command` em cópia separada com `SOURCE_TEST_ROUNDS=5`, `AUTO_FETCH_EDK2=0`, diretório EDK2 propositalmente vazio para não acessar rede. As cinco rodadas executaram **270 testes cada** (total de 1350 execuções), com 0 falhas; status final do comando = 1 **somente** porque não há workspace EDK2 no ambiente de validação. A cópia do log real está no único `Release/Logs/BUILD-COMPILE.log` do ZIP entregue, SHA-256 `74286f6cd614bac0403ebbab4a145e81142af9db71e4ee94e330876d13197dbb`. Esse teste não equivale a build EFI no macOS.

**Patch e procedência:** o único `Audit/diffs/R37-to-R38.patch` foi regenerado a partir do ZIP R37 original e agora inclui as três correções C prévias, o teste novo R38 e a correção do gate. SHA-256 `82b9d76f95960e2570d39145642eafbede21766fcc08a987883f6740cca52a1d`. `git apply --check` na árvore R37 limpa, aplicação e comparação byte a byte com os três arquivos alvo: OK. Os outros 18 arquivos originais mantêm seu conteúdo original R37; o arquivo C, o teste e o log foram modificados conforme documentado, e as permissões dos scripts Python originais foram restauradas.

**Próximo checkpoint:** executar o `.command` corrigido no Mac com o checkout EDK2 correto (`edk2-stable202608`, commit esperado conforme script); só classificar Release como validado se houver compilação efetiva DEBUG/RELEASE, validação dos binários e testes reais de hardware. A cobertura funcional completa do DXE continua **não demonstrada** e as pendências da matriz original permanecem.

## Adendo R39 — retomada comprovada após interrupção (19/10/2026)

> **Este adendo prevalece para os números e intervalos da matriz R39**, preservando a auditoria R38 como histórico. O conteúdo em R38 não foi reescrito. 

### Identidade, escopo e diferenças
- **Base imediata realmente disponível:** `HDAUniversalBootChimeDxe-v1.0.82-R38-GATE-FIXED-FULL-SOURCE.zip`, SHA-256 `568414131bc0cd8e73d6a0ef2c7fdd00c8140705db6a43f4cedc79628fb2f51f`. O R39 mencionado antes desta retomada não estava materializado em arquivo; foi reconstruído a partir do R38 e novamente validado.
- **Base histórica para patch:** ZIP original R37, SHA-256 `7149cc93193fe7cffc80262b493098e9de52940d8b048c66e0aa272f7b91f20f`. Sem pasta `.git` no ZIP real; o repositório Git temporário usado para gerar `git diff` é uma reconstrução local, NÃO uma prova de commit/HEAD upstream.
- **Arquivos:** 21 entradas de arquivo no R37; 24 entradas no R39, incluindo 1 relatório incremental, 1 patch cumulativo e 1 teste novo do R38. Os arquivos originais com bytes diferentes de R37 são: `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c`, `Release/Logs/BUILD-COMPILE.log`, `Tests/test_audit_regressions.py`, `Tools/generate_applealc_profiles.py`. Os únicos arquivos executáveis de projeto alterados em R38→R39 são `Tests/test_audit_regressions.py` e `Tools/generate_applealc_profiles.py`. O C, headers, INIs, PCM, MP3, scripts e README mantiveram bytes R38.
- **Correção de higiene da matriz de cobertura:** o relatório R38 listava 209 funções C e omitia `memcpy` (24–31), `HdaF6KeyNotify` (232–279) e `HdaIsCodecDumpKey` (1228–1231); também atribuía incorretamente a `HdaUniversalBootChimeDxeEntry` a faixa 230–10894. A análise lexical independente R39 encontrou **212 funções C distintas**; a faixa do ponto de entrada é **10743–10894**. A matriz atualizada, de 212 entradas, aparece adiante. Indexação lexical não é prova de revisão funcional de cada corpo nem prova de compilação.
### Causa raiz, alteração mínima e regressões
- **Bug reproduzido em código real do gerador:** `resource_member(folder, path)` usava `PurePosixPath(normalized)` e depois testava partes (`relative.parts`). O construtor normaliza silenciosamente os segmentos `.` e barras repetidas, e remove a barra final. Assim, uma referência de layout ou plataforma como `./layout1.xml`, `layout1.xml/`, `sub//layout1.xml` e `sub/./layout1.xml` podia apontar a um ZIP member canônico, sem que o gerador denunciasse a ambiguidade. O mesmo comportamento foi observado para plataforma. **Antes:** novo teste falhou em 8 subcasos. **Depois:** todos foram rejeitados e o teste passou.
- **Fix direto na origem:** `Tools/generate_applealc_profiles.py` → `resource_member`: acrescentada apenas a condição `or relative.as_posix() != normalized` na validação pré-lookup. É posterior ao tratamento `.zlib` já existente, preservando a semântica original das referências `.zlib`. Não foi alterado o runtime HDA, nem a arquitetura de recursos.
- **Teste acrescentado** ao arquivo já existente `Tests/test_audit_regressions.py`: `AppleAlcGeneratorTests.test_noncanonical_applealc_resource_paths_are_rejected`, com quatro aliases para `Layouts` e quatro para `Platforms`. Não foi criado outro arquivo de teste ou patch. Validações negativas não provam que todos os ZIPs malformados imagináveis sejam recusados.
### Rodadas distintas e limites de evidência
| Evidência | Escopo real | Resultado | Limitação |
|---|---|---|---|
| A: repro falha R38 | `generate()` com oito ZIPs sintéticos apontando a membros canônicos por aliases | 8/8 casos não rejeitados, nova regressão falhou | dados de teste, não archive oficial AppleALC |
| B: fix e teste dirigido | código `resource_member()` corrigido + teste R39 novo | 8/8 aliases rejeitados e teste aprovado | só semântica de caminhos examinada |
| C: regressão geral isolada | `python -B -m unittest discover -s Tests -p test*.py -q` | 271/271 OK | testes mock/estáticos; não firmware real |
| D: gate do script real, isolado | `Build-HDAUniversalBootChimeDxe.command`, `SOURCE_TEST_ROUNDS=5`, `AUTO_FETCH_EDK2=0` | **cinco rodadas 271/271**, 1.355 execuções; todas OK | o próprio script saiu 1 por ausência de EDK2; não compilou EFI |
| E: isolamento sanitizado C | corpo **real** de `HdaGetConnections` compilado com Clang + ASan/UBSan | 6 casos dirigidos + 20.000 entradas host sem erro detectado | mocks de `HdaImmediateCommand`, não MMIO do hardware |
| F: execução Python modo dev | `PYTHONHASHSEED=39 PYTHONMALLOC=debug python3 -X dev -B -m unittest` | 271/271 OK | host Python; não mede resource leaks DXE |
| G: patch replay independente | `git apply --check`; `git apply` sobre cópia R37; `cmp` nos 4 alvos | todos os quatro arquivos byte a byte idênticos ao source final | não certifica EFI |
| H: integridade/sintaxe | `bash -n` 3 scripts; AST de 3 arquivos Python; hashing PCM; ZIP checksum | OK | sem validação em Mac/hardware |
**Tentativas não contadas como rodadas aprovadas:** uma primeira execução do gate abortou na higiene porque a importação externa de teste havia criado `Tests/__pycache__` na cópia de trabalho; um segundo disparo foi interrompido pelo limite de tempo da ferramenta após duas rodadas completas e início da terceira. Ambos permaneceram fora da contabilização de 5/5; foi usada uma **nova cópia isolada limpa** para o gate integral D.
### Auditoria dirigida de funções do C na R39
Funções com leitura/inspeção **direta neste turno**: `memcpy`, `HdaF6KeyNotify`, `HdaAsciiLower`, `HdaAsciiEqualsInsensitive`, `HdaSkipSpaces`, `HdaTrimRight`, `HdaParseNumberStrict`, `HdaParseBoolStrict`, `HdaApplyConfigBool`, `HdaAsciiToUnicode`, `HdaSetNvramAscii`, `HdaCloseFileHandleTerminal`, `HdaCloseEventTerminal`, `HdaCloseTrackedLogFile`, `HdaAcquireBootGuard`, `HdaLog`, `HdaGetConnections`, `HdaPathSearch`, `HdaSnapshotOneNode`, `HdaFindNodeSnapshot`, `HdaCaptureSnapshotValue`, `HdaSnapshotPathNodes`, `HdaSetWidgetPowerD0`, `HdaFindConnectionIndexForNext`, `HdaSelectPcmFormat`, `HdaLegacyRealtekFanoutAllowed`, `HdaRealtekFanoutReason`, `HdaRealtekLegacyPinDriveAllowed`, `HdaComputeOutputPinCtl`, `HdaProgramOutputPin`, `HdaProgramOutputDacStream`, `HdaProgramAnalogOutputPath`, `HdaPathContainsPinIntermediate`, `HdaResetFanoutSelectorClaims`, `HdaIsCodecDumpKey`. A inspeção foi de código e invariantes; **somente** `HdaGetConnections` teve execução host adicional sob ASan/UBSan. As duas rotas `ForcePin` têm evidência histórica separada R38 e os três testes correspondentes continuam sendo executados. O restante da lista de funções C não deve ser chamado de manualmente auditado só porque está na matriz de 212 funções.
### Integridade independente PCM e assets
- `BootChimePcm.h`: 243252 amostras `INT16`, mínimo -28737, máximo 28432; constante declarada confere; SHA igual ao R37; dados intocados.
- `BootChimePcm44.h`: 223488 amostras `INT16`, mínimo -28727, máximo 28422; constante declarada confere; SHA igual ao R37; dados intocados.
- `assets/BootChime.mp3`: SHA-256 `7914bd202b1f539d1d8aafb7a371ee8008936bc5323094e52ca6439cf411e3ad` igual ao R37. Isso atesta conservação dos bytes, não fidelidade sonora após boot.
### Inventário dos 24 arquivos, hashes integrais e estágio de comprovação
Legenda: **I** = indexado/conferido (não revisado funcionalmente por completo); **D** = dados validados estruturalmente; **LM/AP** = leitura/análise manual **dirigida**; **AM** = análise manual do módulo/fluxo indicado; **E** = execução de testes; **V** = validação do artefato no host; **M** = relatório. Nenhuma destas siglas equivale a Release validado.
| Arquivo | Linhas ou binário (bytes) | SHA-256 R39 | Etapa | Dependência / risco | Evidência ou pendência |
|---|---:|---|---|---|---|
| `Audit/R38-AUDIT.md` | 728 | `auto-referência evitada; calcular externamente` | M | único relatório incremental de auditoria | histórico preservado + adendo R39 |
| `Audit/diffs/R37-to-R38.patch` | 280 | `f79c9f8fcf678844453be85c8a9e3b7f52367770a4ede34ac8e398963dcce435` | V | patch único cumulativo R37→R39; nome histórico conservado | git apply --check; replay byte a byte dos quatro alvos |
| `Build-HDAUniversalBootChimeDxe.command` | 900 | `d004d3105805ef9295b2d005105812b5f08361807b90a6766f165ce4405d364c` | LM | preflight; higiene; setup EDK2; build/validador PE | pipeline 5x271 até gate EDK2; build EFI não executado |
| `CHANGELOG.md` | 417 | `2d26c0aa4ca11bf02c124eca3d06dc970d7e8a53a35db070b9fe7f855dbe7ad3` | I | changelog histórico | byte idêntico R37; leitura integral atual pendente |
| `Enable-HDAUCodecDump-NextBoot.command` | 243 | `c7f7cd4706733cd7696832b654a5c4431e700745036b3f747fa01e4ef336747a` | I | manipulação EFI/config em macOS | sintaxe e testes estáticos; execução macOS pendente |
| `HDAUChimeDebug.ini` | 59 | `fa7698619bf07ba1840ccc67c676ac3df2447ee318ddad6a85e4df36b76da246` | I | flags e defaults | conteúdo preservado, parsing no firmware pendente |
| `HDAUChimeRelease.ini` | 59 | `901561bc324f520002c82d5d02e3b29cc0f3ee44a0baf0283397fe3b66ee57e7` | I | flags e defaults | conteúdo preservado, parsing no firmware pendente |
| `HDAUniversalBootChimeDxe/BootChimePcm.h` | 20293 | `68220bf126b43627375ba27b6ef0cdbe7f8ceaa96b8ceb7c57b45c483e6a5c43` | D | PCM e bounds | 243252 amostras verificadas, preservação SHA R37 |
| `HDAUniversalBootChimeDxe/BootChimePcm44.h` | 18646 | `1da445f511019536d2397373b5c4fb24dbe1ff38fe0ebdfafda039d030331d04` | D | PCM fallback e bounds | 223488 amostras verificadas, preservação SHA R37 |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` | 10894 | `2537228ca89640673f9b349bf170774b79ea3fd9f9c8a76ad4895b9e8966f96e` | AP | HDA MMIO/DMA/TPL/lifecycle, restore, codec/stream | revisão dirigida; host C HdaGetConnections ASan/UBSan; firmware pendente |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf` | 45 | `222f8f58ce0e519069bc2ee35b35848f141922cf725ffe334bebb1305012bff3` | I | EDK2 drivers/LibraryClasses | preservado; compilação pendente |
| `HDAUniversalBootChimeDxe/HdaAppleAlcLayouts.h` | 3672 | `bd348bd45382a3b3bfa5902f2b56ca088b23a0418bf6d157a60046c72dd560a1` | I | dados gerados com origem AppleALC fora do ZIP | intactos vs R37 e teste de offsets; equivalência upstream pendente |
| `HDAUniversalBootChimeDxe/HdaUefi.h` | 577 | `b6357b966e78babecfa497711851c099409a51192e0f38e11f917e1b5ec578f5` | AP | constantes de NID e capacidade; contrato com C | confrontadas 7-bit NID e arrays; headers por completo pendentes |
| `HDAUniversalBootChimeDxePkg.dsc` | 33 | `5ada46b1cb0eba180ce068d45d4ca54e96ba5ad4f7c39b180a8b5f236589a174` | I | dependências/LibraryClasses EDK2 | preservado; compilação pendente |
| `README.md` | 608 | `f3d9a12ab86c078a7eb712a6e982f1977fb5e399ea83a30b60fffc643d388257` | I | documentação histórica | byte idêntico R37; leitura integral atual pendente |
| `Release/Logs/BUILD-COMPILE.log` | 1400 | `84fda8ed9addfcdc79c5a74f1d3dfac6bb9f6a29a82d7d6910dd4b6094fa5db4` | V | log único de gate | 5 rounds 271/271; parada no EDK2 |
| `Setup-EDK2.command` | 85 | `6a515d741b0ef47cff696a08d1b9aca4f5aae2ad558c9dfd41674994e56318c4` | I | bootstrap de ferramenta; rede/checkout | sintaxe; instalação real pendente |
| `Tests/test_audit_regressions.py` | 4074 | `7dc7a512978925ccf4ee83417c1646e8068f18058f3849463af14a3bfacfd7d2` | AM | instrumentação e regressões, parser C de testes | novo teste pre/post e suite 271/271 |
| `Tests/test_forcepin_runtime.py` | 171 | `00d77fb92e43e0b97e50ad7eeb5406613421ffa3dc0aad1a22cfbef94cba16fb` | E | harness de ForcePin + snapshot | 3 testes reproduzidos na suíte 271/271 |
| `Tools/generate_applealc_profiles.py` | 765 | `a2d672acd06a2780f8ba37f3d4c32f46d6b87f872944db0c39e8bf09d53ee70f` | AM | ZIP path, nomes, plist, controle de tamanho, perfis | novo teste oito casos + 271/271; sem AppleALC upstream |
| `assets/BootChime.mp3` | 30407 bytes | `7914bd202b1f539d1d8aafb7a371ee8008936bc5323094e52ca6439cf411e3ad` | D | áudio MP3 original | byte idêntico R37; reprodução física pendente |
| `docs/NO-SHELL-EFI-TEST.md` | 53 | `892dbf1611ea71e258c234453a8942539b1a7efc8afda61f4c8eded5c1b2d5e8` | I | roteiro de hardware | byte idêntico R37; teste real pendente |
| `docs/PROJECT-REPORT.txt` | 370 | `24c7735d7506a1dbf1c290165faf54eb949a484a25d54399439f01c2b6ea543c` | I | relatório histórico | byte idêntico R37; leitura integral atual pendente |
| `docs/TESTING.md` | 81 | `4adf5045d95beeb66d03fdd9749ff0d744ae3430293042f9b0dd1bf1e43a6b40` | I | metodologia de testes | byte idêntico R37; cobertura completa pendente |
### Matriz integral corrigida das definições C (212 entradas)
A coluna “análise” é propositalmente restritiva. `AR38` = evidência dirigida documentada no histórico R38, **não** inspecionada de novo integralmente nesta rodada; `AM39` = examinada em nova revisão dirigida; `VHOST39` = corpo da função executado isoladamente com mock. `I` = somente indexação nova e/ou evidência de teste de suíte, **não análise individual concluída**. Hash é do trecho desde o nome da função até o `}` final, sem qualificador anterior ao nome; utilitário lexical ignora comentários e literais para localizar as chaves.
| Função C | Linhas (nome até fim) | SHA-256 bloco | Análise | Dependência/risco e evidência |
|---|---:|---|---|---|
| `memcpy` | 24–31 | `e39dac3401e425c380488f48` | AM39 | CopyMem; memcpy libc-free DXE; retorno igual destino |
| `HdaArmDeferredCodecDumpTimer` | 217–224 | `89764733dd8926764518e0d6` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAsciiLower` | 226–229 | `689dac52e689a0c40988419e` | AM39 | comparação ASCII; não modifica string |
| `HdaF6KeyNotify` | 232–279 | `6b2d71e8b73ab14246e7bb8a` | AM39 | barreira TPL, depth, reentrância e timer; firmware ainda pendente |
| `HdaAsciiEqualsInsensitive` | 282–290 | `54c81ec46784e50d45ee1fc9` | AM39 | leitura até NUL, short-circuit |
| `HdaSkipSpaces` | 292–295 | `7690b0923cd998f2e8679fcb` | AM39 | retirada de espaços INI |
| `HdaTrimRight` | 297–307 | `b6919f74bfcea64da3a5a04d` | AM39 | ASCII trim in-place; precondição de terminador |
| `HdaParseNumberStrict` | 309–343 | `abc36b7b5b42a9a391124d0b` | AM39 | overflow UINTN, base 10/16 e rejeição de sufixos |
| `HdaParseBoolStrict` | 345–364 | `f32f83561eaa51192378c9b0` | AM39 | semântica booleana / parse estrito |
| `HdaApplyConfigBool` | 366–372 | `0cca9aa06d810f2bae201b4f` | AM39 | grava somente após parse estrito |
| `HdaAsciiToUnicode` | 374–381 | `80c75e434bf7a022d9442515` | AM39 | limites de buffer e NUL |
| `HdaSetNvramAscii` | 383–394 | `d32db0dc681e92f8f83676d2` | AM39 | SetVariable; retorno deliberadamente ignorado e toggle desligado por padrão |
| `HdaCloseFileHandleTerminal` | 398–416 | `efed9df665874f067c80d0c7` | AM39 | ownership do File.Close terminal |
| `HdaCloseEventTerminal` | 418–436 | `33a9f8366c03012c27f1a596` | AM39 | ownership do CloseEvent terminal |
| `HdaCloseTrackedLogFile` | 438–466 | `3227103cbe41433b72588f8b` | AM39 | Flush/Close, status e nulificação de handle |
| `HdaAcquireBootGuard` | 468–554 | `c83632b659f893d0722f5f34` | AM39 | guardas NVRAM/protocolo e falha fechada |
| `HdaLog` | 556–603 | `89694f989d05f5ecd6e07654` | AM39 | escrita, flush e contenção de falhas |
| `HdaTraceStage` | 607–624 | `fca89869dbb6fd093448e67d` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSetLastStatus` | 626–629 | `6b0f504be38b6081df8863af` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaApplyConfigKey` | 631–749 | `37aabcfd49a101f509dc03ae` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaParseConfigBuffer` | 751–779 | `274a77d10a4f1fd9b1683923` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaReadConfigFromPath` | 781–841 | `f13804f145c7ffc8072acebd` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaOpenOrCreateDir` | 843–876 | `9b5700b83d55391c5aefbe33` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaOpenDebugLog` | 878–996 | `f9ab504fcab5d37bd36fe252` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAsciiStartsWithInsensitive` | 1000–1009 | `151408b0eed4372c9b333149` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaBootArgTokenBoundary` | 1011–1013 | `d4760d1186e2f85f32c4110d` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaFindAlcIdInAscii` | 1015–1063 | `7a199dd3a59f9cd3639f116e` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaBootArgIsEnabledToken` | 1067–1086 | `809a01d5bdddd3fe2c498f8a` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaFindCodecDumpRequestInAscii` | 1088–1109 | `f5e9f16f34fcd5f7b970e2ad` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaConvertUtf16BootArgsInPlace` | 1111–1128 | `bd6eaa7833b81fb9071761ff` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaReadAlcIdFromBootArgs` | 1130–1224 | `d7f7357483d3433dd73c6dba` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaIsCodecDumpKey` | 1228–1231 | `a8683d3d494c9b7754c17822` | AM39 | critério de tecla; sem I/O no predicado |
| `HdaDeferredCodecDumpNotify` | 1233–1327 | `3f52ab10c3a01934156e6151` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaServiceDeferredCodecDumpIfSafe` | 1329–1340 | `df53144c527ad0be01a73c6c` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaUninstallBootScreenDNotify` | 1342–1404 | `6e1be0657c26eeb0992a9bc1` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaInstallBootScreenDNotify` | 1406–1571 | `617dcb2d004c0a3d61297ba2` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCleanupAsyncPlayback` | 1575–1709 | `0ea29fcf23f9ca55a5fa2cf6` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaExitBootServicesCleanup` | 1711–1754 | `8baf35838cb60c537d5e683e` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaInstallExitBootServicesCleanup` | 1757–1788 | `391913800be49ddca2b643cc` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPollForF6` | 1790–1858 | `dbe006531ef2445ca1cb3293` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDebugInit` | 1860–1932 | `b2bcdde9f3cd9cd8b735b87e` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDebugFinish` | 1934–1939 | `52911220a90d760b262ecd3b` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaReadPciInfo` | 1942–1961 | `3aab0577b57a80906035dbb9` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaNormalizeExactSuccess` | 1963–1966 | `1b31cc66456794519e4b5136` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPciFlushExact` | 1968–1971 | `a8b3b608c2763e3f52486a22` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaMemRead8` | 1973–1978 | `966b0a33583fcfc4f70237e7` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaMemRead16` | 1980–1985 | `b5b731a1a4fabef1f63bdca4` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRequireNoRunningStreamsBeforeBusMasterEnable` | 1987–2023 | `f9aba9a47decb0398587004b` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaMemRead32` | 2025–2030 | `2d626bcf6d3e1172e906b0fa` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaMemWrite8` | 2032–2037 | `89f92313e3c9de671e5f3cee` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaMemWrite16` | 2039–2044 | `324fbaa33594d625e441ad40` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaMemWrite32` | 2046–2051 | `be71c7fa4620c1c8a3970065` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRequireControllerResetQuiescent` | 2053–2100 | `530ee2fbf878475a0d2fe433` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaStall` | 2102–2122 | `237bdb1955a9b3784dc1107a` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaControllerReset` | 2124–2175 | `bcd973840f8a3a2cb888c4a8` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaControllerEnsureReady` | 2179–2224 | `389de567736936a0553400f8` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAbortImmediateCommand` | 2226–2285 | `43f9af84e572cbdc679b181a` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAcknowledgeImmediateResult` | 2287–2309 | `3229cf9735584fbfec074ff3` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaImmediateCommandRaw` | 2311–2419 | `e8d560a95399ddc387267aef` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCorbRirbStatePending` | 2421–2427 | `538c0b4c4380eb5d5632b62d` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDischargeRetainedCorbRirbBusMasterRestore` | 2429–2459 | `29c899380fab6956c8384b8f` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRequireCorbRirbSnapshotUnchanged` | 2461–2552 | `e8a0baf354a0e90f6576e780` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaVerifyTemporaryCorbRirbProgramming` | 2554–2636 | `b38f8c55a0938b25d68fb580` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaVerifyCorbRirbDmaEnabled` | 2638–2665 | `a965e072a531228273cc312d` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaResetCorbReadPointer` | 2667–2701 | `40c0de0d3f15e7715715457f` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaResetRirbWritePointer` | 2703–2719 | `a4020795862dcff2d79b39b2` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCleanupCorbRirbState` | 2721–2979 | `484c2ad0a82b00bc14d686ef` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCleanupRetainedCorbRirb` | 2981–2993 | `cfc53bb28d7cfb82f9e37abd` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCorbRirbCommand` | 2995–3457 | `f2a7eab9b627ea52206a3a91` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaImmediateCommand` | 3459–3486 | `6abd2510f15dcfa9b7c4b8dd` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaGetParamChecked` | 3489–3503 | `51c4f8edcbc7a93a149a301a` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaGetParam` | 3505–3509 | `246c7a7e63bac579284b696d` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSetVerb` | 3511–3520 | `879d086dd088561c385a3413` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaWidgetType` | 3522–3524 | `713be1553fa8b2d9a008fce6` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAmpUnityGainFromCaps` | 3527–3537 | `345520bb06ec3f5a5ca67aca` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAmpOutUnmuteUnityFromCaps` | 3539–3541 | `c5a1e4d3393f18db3037989a` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAmpInUnmuteUnityFromCaps` | 3543–3554 | `cc58dcdc2d1ed8e0be75c846` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaFindAlcIdProfile` | 3644–3657 | `98bc404cdfc069a340a3fc50` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaResolveAlcIdProfile` | 3659–3662 | `44ae69e68df543be1521f4b6` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaFindAlcIdConfigProfile` | 3664–3677 | `1bbf0c6787d0fd5dfe10ab23` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaConfigVerbIsRollbackSafe` | 3679–3738 | `f3b3af8d8554a8da642cf7b3` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaConfigSpanIsRollbackSafe` | 3740–3754 | `bdaf332ecf173776b1ce0970` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaApplyAlcIdPrebootVerbs` | 3756–3805 | `08bc17d3388b8b8ce4b0b5a1` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaApplyExactProfileVerbs` | 3807–3863 | `63f8ba0f85eb0348b21a40ec` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaFindCodecPolicy` | 3865–3881 | `acb753864b0a3378d9bececa` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDefaultDevice` | 3883–3885 | `edce40a4397fb6e41fb7d7f9` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDefaultPortConnectivity` | 3887–3889 | `ef26f34263dda193c69b2be1` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDefaultAssociation` | 3891–3893 | `0a9297e96b33b44a43f985d0` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDefaultSequence` | 3895–3897 | `010e9e20c00149e5568404e2` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDefaultConnectionType` | 3899–3901 | `027eb548618ce8a62b1f6622` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDefaultLocation` | 3903–3905 | `f6b6e7537a8fc0911a7e32e7` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDefaultColor` | 3907–3909 | `45fe8f03a11817d60a67a90e` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPinDefaultIsDigital` | 3911–3918 | `92b31d51a135760f3f5f63d6` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPinDefaultLooksConnected` | 3920–3930 | `2f288e5884a8f37148a35471` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaGetSubnodes` | 3932–3957 | `d9b20133ca53196803856d0e` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaGetConnections` | 3959–4042 | `72fa82ab776d1086a6369b24` | VHOST39 | retornos HDA, ranges, erros, limites e buffer; **execução host ASan/UBSan 20006 casos** |
| `HdaDumpWrite` | 4045–4055 | `f4d95dd26207f66fb50a5d9f` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpWidgetTypeName` | 4058–4071 | `2eb50a092d197e94f4a96181` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpDefaultDeviceName` | 4073–4092 | `787954f2138e5f794ae4f197` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpConnTypeName` | 4094–4111 | `157baf6c3d010010c07c2772` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpPortConnName` | 4113–4121 | `ec15380418a8019fba135eb4` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaOpenLoadedImageRoot` | 4123–4150 | `e9889d360eba75e4c9900455` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpConnectionList` | 4155–4170 | `1385282b15c496fb02a5b029` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaEnsureAppendLog` | 4173–4196 | `dd9dfaae95a11f6b2f973ad5` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCloseAppendLog` | 4198–4203 | `cd4909e94e21ec5520ab3361` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpNodeFull` | 4205–4291 | `35cb49858ae2cc10a9623ee1` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpCodecFull` | 4293–4370 | `65de404e3aba555b323cf545` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpControllerFull` | 4372–4533 | `2b97078ce3f769b5ddbfef09` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPathSearch` | 4535–4621 | `27cb87110abd0a2c0e60a1de` | AM39+AR38 | fila BFS, visited, parent e cotas HDA |
| `HdaReadPinSense` | 4623–4647 | `cdee576ce019706c672d5b91` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPinPriority` | 4649–4805 | `afb99ad62c404dadfe94176a` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaClassifySelectedRoute` | 4815–4852 | `c8d72c9cfe9d3a24a6724883` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaValidateFullRoute` | 4854–4886 | `342d7855767b066ee7f3edc2` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaTryFullAlcIdRoute` | 4888–4961 | `f055998b389d768f360897f6` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaTryAlcIdProfilePath` | 4965–5112 | `c07c73e676cb0990d18834aa` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaTryQuickPath` | 5114–5252 | `d958c9bad54c4cb6acb5ce28` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaFindPlayablePath` | 5255–5403 | `ab80c6bce1c9af308fb042fb` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRouteCandidateEquals` | 5406–5418 | `e1bfcfe200bfac04e0846550` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAddRouteCandidate` | 5420–5479 | `d12e972288f6fccbc20e1780` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSortRouteCandidates` | 5481–5496 | `13ed3289834e4f618e2a7c02` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCollectRouteCandidates` | 5498–5544 | `462a3f2b4bf21e36df9ad7dc` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaLoadRouteCandidate` | 5546–5573 | `a2c31ce5dba545f241c6ca85` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaGetEffectiveAmpCapsChecked` | 5576–5590 | `451e539c1e56aed20c0a292d` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPathInputAmpsProgrammable` | 5592–5634 | `e0ebdcb65ed259f128d6d166` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaProgramSelectedInputAmp` | 5636–5663 | `5711801e94cd0d11d48aaf6f` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaProgramConnectionPath` | 5665–5722 | `a7b6fec0f5c1d11817e3cafa` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSelectConnectionForPath` | 5724–5728 | `d501590781419f585b7ea306` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaProgramAmpSafeUnity` | 5730–5786 | `58727bd89ad15dcf52b53eac` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAmpVolumeGainFromCaps` | 5804–5843 | `a99fadbc929946393c314569` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaVerifyAmpGain` | 5845–5881 | `c73050e25097d820f234702c` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaProgramStoredHardwareVolume` | 5883–5955 | `2fd0f7191c9bb3a17174e3e4` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaTryHardwareOutputVolume` | 5957–5979 | `573cd4ad55ffc4da4bd42cd0` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaTryHardwareInputVolume` | 5981–6018 | `192ea4a30162319ed48d2991` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaProgramPlaybackVolume` | 6020–6057 | `644eeaa1ff1f061f77c3a398` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaU8ArrayContains` | 6059–6066 | `7610a1650e46b8a2bca3853a` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaIsOutputPin` | 6068–6077 | `ec0a31138add17ac00988994` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSetPowerD0AndWait` | 6079–6096 | `6b4488c9e82ad98fd0fa3cc7` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSetWidgetPowerD0` | 6098–6107 | `6973cbd61125145ead102ceb` | AM39+AR38 | capacidade e tratamento de erro |
| `HdaFindConnectionIndexForNext` | 6109–6129 | `9b3692ecb39bfdda76443681` | AM39 | índice e validação de próximo NID |
| `HdaSnapshotOneNode` | 6131–6318 | `d98e45820f514d310e432bc9` | AM39+AR38 | snapshot parcial/ownership do node e verificações posteriores |
| `HdaFindNodeSnapshot` | 6320–6327 | `ae3cf11868d5f2a1e24744af` | AM39 | busca em snapshots e nulidade |
| `HdaCaptureSnapshotValue` | 6329–6347 | `3ca98f8b5d4309e0f20b864d` | AM39 | flag somente após GET |
| `HdaSnapshotPathNodes` | 6349–6419 | `a12b9e0dac37392d5599f2a0` | AM39+AR38 | confere POWER/AMP/PIN/EAPD/DAC antes de mutação |
| `HdaSnapshotWarmResetPower` | 6421–6461 | `c1ad156e11047670806077ac` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCaptureProfileVerbState` | 6463–6597 | `0c83e6958008aaa549fcf250` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaGetCoefIndexSnapshot` | 6599–6618 | `ef16c62a0b73d6e8bcb6db36` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSnapshotCoefficientValue` | 6620–6643 | `47010f85e78965670ddda1e7` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSnapshotCoefficientSpan` | 6645–6722 | `d754ffd38b52654ad10eac42` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSnapshotConfigProfileState` | 6724–6759 | `537ad0a19406a37c735dbeb0` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSnapshotCodecState` | 6761–6901 | `7c4d327e72995accce648e5b` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRestoreCodecState` | 6903–7142 | `0f5990f0c2adb53aab592e2d` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRestoreCodecStateRetry` | 7144–7163 | `61d9acca20b85eafcd2e7353` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaIsAudioOutputDac` | 7165–7194 | `6ef11f529149eb926ccea6d6` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaSelectPcmFormat` | 7196–7253 | `3154a307ff67d71acebd36b0` | AM39 | caps HDA, fallback, canais/frames/overflow |
| `HdaCollectAudioOutputDacs` | 7255–7281 | `70b78c28ef366426039cd4a7` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaBuildAlcIdDacOrder` | 7283–7314 | `fb1e04eb84f12feb4b97a76b` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaLegacyRealtekFanoutAllowed` | 7316–7322 | `d84b5011d7f30bb2f4951310` | AM39+AR38 | policy, ForcePin e overrides |
| `HdaRealtekFanoutReason` | 7324–7328 | `ccfb0939ea47c204183df142` | AM39 | cadeia diagnóstica |
| `HdaRealtekLegacyPinDriveAllowed` | 7330–7334 | `b32ba773dd359c03c48fd75b` | AM39 | policy + flag |
| `HdaComputeOutputPinCtl` | 7339–7375 | `ac5c90945e2bde172794c55b` | AM39 | config-default e política HP |
| `HdaLogSelectedPathState` | 7377–7422 | `87eb7ecf01d829b0af13f24a` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaProgramOutputPin` | 7424–7522 | `1b9b2a168f649b12eddd471e` | AM39+AR38 | GET PIN CAP, config, SET PIN/EAPD e readback |
| `HdaProgramOutputDacStream` | 7525–7555 | `cb938fe15572e0e92465adf3` | AM39 | stream tag/format e validação de DAC |
| `HdaProgramAnalogOutputPath` | 7558–7600 | `71c522b88acc12fa8f2ce31a` | AM39+AR38 | checagem digital e programação em cadeia |
| `HdaRealtekGpioAmpInit` | 7608–7616 | `2453ecf04a362728ff0133ef` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPathContainsPinIntermediate` | 7633–7654 | `7ec3a4a6ebcea3fbc2466c89` | AM39+AR38 | falha fechada em widget inesperado |
| `HdaResetFanoutSelectorClaims` | 7656–7687 | `b4c1cb92a6ad630eec9be4f1` | AM39 | limpa claims e lê caps antes de publicar estado |
| `HdaPathsSelectorCompatible` | 7689–7729 | `b80376a068a77457d2ba62e7` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCommitFanoutSelectorClaims` | 7731–7759 | `049e9fc6a8abc34a7fc7f1ab` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRealtekBuildDacPreferenceOrder` | 7761–7788 | `58006ad357be3bbcf3dcb8a3` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRealtekProgramPinDacProfile` | 7791–7852 | `88be47007a5caeb6e907e955` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRealtekCodecWarmReset` | 7854–7878 | `2e29839d27fd516522ff8b0c` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAlcIdLayoutSafeFanout` | 7880–8011 | `de187fa16a1db838c87d1022` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRealtekAppleAlcComboInit` | 8013–8084 | `fd1b88607edb3ccae500c7d2` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaEnableLegacyRealtekFanout` | 8086–8217 | `dfd18c36577fcc7965be98af` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaLogAlcIdFanoutState` | 8219–8239 | `1cebbafff7a880a0428589a4` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaProgramHdmiInfoFrame` | 8241–8281 | `ebc98c328c46fb1ad8a9db28` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaProgramDigitalOutputPath` | 8283–8350 | `5483cc5ff7b7b17ccc7744d8` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPostStartCodecReapply` | 8352–8384 | `b0bd1c1bc27cb24ef05570fd` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaUnmutePath` | 8387–8447 | `df79e87afe91d8e4007bc95d` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCopyBootChime` | 8450–8479 | `c24d0265e38b93f221ad9bc1` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAllocatePlaybackBuffers` | 8481–8703 | `f751e3a06bf16ea8368d66bf` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaFreePlaybackBuffers` | 8705–8798 | `5b4017a270746de18b0d28e0` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaFinalizePlaybackContext` | 8800–8828 | `f97478d8c6e041b738fac0ae` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaCaptureIdleStreamState` | 8830–8886 | `0b8c268f5fd15e47cdfdb1b4` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRequireIdleStreamSnapshotUnchanged` | 8888–8936 | `1bea5c72d6ba16a28afeba1c` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRequireStreamTagAvailable` | 8938–8982 | `941eeb4b63497d1c2a95126d` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaResetStreamDescriptor` | 8984–9016 | `f2b6f91e5035838d9ad3bb66` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaWriteStreamLviPreserve` | 9018–9031 | `d1b554b1aec8678f1dcae30f` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaWriteStreamFormatPreserve` | 9033–9047 | `46e562c6ec3649948d600145` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaVerifyPreparedOutputStream` | 9049–9101 | `cfc6c936b3fc06e8b0893d56` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaVerifyRestoredStreamDescriptorState` | 9103–9165 | `f49eaec5d48c1c5e73e28f44` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRestoreStreamDescriptorState` | 9167–9216 | `ad4799de936382758cbea0ef` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPrepareOutputStream` | 9218–9403 | `38892f928625fdd71439571d` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaMonitorPlaybackProgress` | 9406–9501 | `2c92f3fef293514ecf01b56f` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaStartOutputStream` | 9503–9535 | `050926cf86515de1978fa9dd` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaGetPciAttributesExact` | 9538–9551 | `43f5e565ff268e4f03fa8cf0` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaVerifyPciAttributeBits` | 9553–9567 | `78371c557acce9b892043809` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaApplyPciAttributeBits` | 9569–9617 | `3ca19400c531697e05a79b7f` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRestorePciAttributes` | 9619–9654 | `3aa185d5e0c1a203b63f9e5c` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaRetainContextForCleanup` | 9657–9673 | `2ac81e5f174dbc8e4909b7c3` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaStopOutputStream` | 9675–9792 | `aa5e487da0320d84460cde37` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaReleasePreparedStreamAndBuffers` | 9794–9812 | `a9009acbac79cd605d4c2466` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaAsyncPlaybackTick` | 9815–10001 | `c72d7d5c49d84b64c1b443b2` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaStartFastBootPlayback` | 10003–10060 | `766e25e04096a2bf9b99a41f` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaPlayChime` | 10062–10326 | `b80d4ae1a5d75f35fbff33c2` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `PciIsHdaController` | 10328–10337 | `7b5ffd63823df8b8abf13b3a` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpAllControllers` | 10340–10451 | `4b3dacd1bd15b5f083f8d60f` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpAllControllersDynamic` | 10454–10480 | `180ad26732c1e560d40ed12f` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaDumpCodecBasic` | 10482–10520 | `0923ca91302cc376e72a45a9` | I | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaTryController` | 10522–10739 | `8dfd49e4f8c295dbcfb81a4d` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
| `HdaUniversalBootChimeDxeEntry` | 10743–10894 | `fed4bd22ee6d770a2a17215f` | AR38 | Ver matriz R38 e pendências: sem execução individual desta função |
### Matriz R39 de definições Python (produção e testes)
Esta tabela substitui as posições antigas de R38 para os arquivos Python. `E` para `test_*` comprova apenas que o caso executou e suas asserções passaram; `I` significa indexação AST; `AM39` identifica inspeção manual dirigida e/ou correção. Análise profunda de todas as funções de testes continua pendente.
| Arquivo | Definição | Linhas | Estágio | Escopo |
|---|---|---:|---|---|
| `Tools/generate_applealc_profiles.py` | `validate_plist_tree` | 43–57 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `parse_plist_bytes` | 77–82 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `load_plist_fragment` | 85–96 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `load_plist` | 99–102 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `as_int` | 104–117 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `leaf_node_chains` | 120–140 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `c_string` | 142–145 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `resource_member` | 148–159 | AM39 | join/validação ZIP + regressão |
| `Tools/generate_applealc_profiles.py` | `read_plist_member` | 162–176 | AM39 | join/validação ZIP + regressão |
| `Tools/generate_applealc_profiles.py` | `resource_root` | 179–190 | AM39 | join/validação ZIP + regressão |
| `Tools/generate_applealc_profiles.py` | `pinconfigs_member` | 193–200 | AM39 | join/validação ZIP + regressão |
| `Tools/generate_applealc_profiles.py` | `config_name` | 203–208 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `masked_constraints_overlap` | 211–214 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `exact_profile_is_subset` | 217–228 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `exact_profile_specificity` | 231–234 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `generate` | 237–707 | AM39 | join/validação ZIP + regressão |
| `Tools/generate_applealc_profiles.py` | `generate.verb_span` | 634–645 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `write_output_atomic` | 710–738 | I | lida no módulo, revisão profunda pendente |
| `Tools/generate_applealc_profiles.py` | `main` | 741–761 | I | lida no módulo, revisão profunda pendente |
| `Tests/test_audit_regressions.py` | `load_generator` | 27–33 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `plist_bytes` | 39–40 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `make_archive` | 43–75 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `exact_entry` | 78–90 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `extract_c_function` | 93–107 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `extract_c_function_definition` | 110–125 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_valid_generic_profile_is_generated` | 129–147 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_equivalent_mask_domains_with_conflicting_verbs_are_rejected` | 149–160 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_strict_subset_exact_profile_remains_valid` | 162–173 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_unsafe_resource_path_is_rejected` | 175–180 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_noncanonical_applealc_resource_paths_are_rejected` | 182–216 | E+AM39 | pre/post 8 casos de alias |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_exact_overlap_checks_skip_unrelated_layout_domains` | 218–278 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_exact_overlap_checks_skip_unrelated_layout_domains.counted_overlap` | 267–270 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_oversized_configdata_is_rejected_before_verb_materialization` | 280–298 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_exact_profile_group_has_quadratic_work_safety_cap` | 300–317 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_duplicate_archive_members_are_rejected` | 319–329 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_malformed_plist_parser_errors_are_normalized` | 331–341 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_malformed_plist_fragment_parser_errors_are_normalized` | 343–346 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AppleAlcGeneratorTests.test_malformed_optional_layout_keeps_existing_skip_semantics` | 348–363 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `GeneratedHeaderTests.setUpClass` | 368–369 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `GeneratedHeaderTests.array_body` | 371–376 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `GeneratedHeaderTests.test_profile_pair_arrays_match_c_capacity` | 378–388 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `GeneratedHeaderTests.test_generated_offsets_stay_inside_backing_arrays` | 390–416 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AsyncOwnershipTests.setUpClass` | 421–422 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `AsyncOwnershipTests.test_timer_arm_failure_uses_central_cleanup_and_aborts_reuse` | 424–428 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AsyncOwnershipTests.test_blocking_fallback_is_suppressed_after_ownership_transfer` | 430–435 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AsyncOwnershipTests.test_central_cleanup_does_not_free_event_owned_buffers` | 437–446 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AsyncOwnershipTests.test_repeated_callback_after_failed_close_avoids_stream_registers` | 448–453 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `AsyncOwnershipTests.test_callback_releases_buffers_only_after_event_close_success` | 455–463 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `CorbRirbHardeningTests.setUpClass` | 468–471 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `CorbRirbHardeningTests.test_retained_ownership_has_exit_boot_services_retry` | 473–484 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `CorbRirbHardeningTests.test_corb_rirb_device_addresses_require_128_byte_alignment` | 486–490 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `CorbRirbHardeningTests.test_corb_rirb_respect_gcap_64ok` | 492–496 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `CorbRirbHardeningTests.test_unsolicited_rirb_responses_are_ignored` | 498–503 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `CorbRirbHardeningTests.test_corb_read_pointer_reset_is_handshaked` | 505–511 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `CorbRirbHardeningTests.test_dispatch_waits_for_pci_io_and_guard_is_after_handle_discovery` | 513–517 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `CorbRirbHardeningTests.test_plist_member_read_errors_are_normalized` | 519–528 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `CorbRirbHardeningTests.test_atomic_output_failure_cleans_temporary_file` | 530–537 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.setUpClass` | 542–546 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_failed_corb_rirb_calls_never_publish_unowned_outputs` | 548–572 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_failed_playback_map_never_publishes_unowned_mapping` | 574–591 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_failed_console_protocol_lookup_cannot_publish_invalid_protocol` | 593–597 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_failed_locate_handle_buffer_is_never_freed` | 599–607 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_failed_log_open_cannot_publish_invalid_global_handle` | 609–615 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_directory_retry_clears_failed_open_output` | 617–627 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_loaded_image_root_publishes_handle_only_after_success` | 629–638 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_failed_single_shot_protocol_install_clears_output_handle` | 640–649 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_generator_groups_exact_overlap_checks_by_codec_layout` | 651–669 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_generator_rejects_oversized_archive_before_zip_parsing` | 671–677 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_generator_rejects_total_uncompressed_archive_limit` | 679–690 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_generator_plist_depth_and_object_limits_are_bounded_iteratively` | 692–705 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_fixed_volume_copy_has_no_dead_gain_assignment` | 707–710 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_audio_dma_device_range_cannot_wrap_before_bdl_split` | 712–720 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_bdl_has_at_least_two_entries_and_lvi_matches` | 722–729 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_fastboot_requires_exit_boot_services_cleanup_before_transfer` | 731–736 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_terminal_cleanup_minimizes_external_work_at_exit_boot_services` | 738–746 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_exit_boot_services_never_reenables_ring_dma` | 748–752 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_largest_supported_corb_rirb_ring_is_preferred` | 754–760 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_rirb_overrun_is_detected_and_not_treated_as_timeout` | 762–769 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_configdata_write_failure_is_propagated` | 771–775 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_coefficient_index_uses_4bit_verb_16bit_payload` | 777–785 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_generated_profiles_fit_coefficient_snapshot_capacity` | 787–816 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_coefficient_snapshot_restores_reused_index_snapshot` | 818–823 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_exact_profile_is_snapshotted_or_skipped_before_application` | 825–831 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_exact_profile_code_does_not_reference_nonexistent_name_field` | 833–835 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_general_hardware_access_requires_exit_boot_services_cleanup_hook` | 837–849 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_controller_iteration_stops_on_any_retained_hardware_ownership` | 851–858 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_deferred_codec_dump_cannot_reenter_controller_critical_section` | 860–865 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_controller_critical_section_services_deferred_dump_after_owner_returns` | 867–878 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_interactive_dump_notify_is_installed_after_config_requested_dump` | 880–888 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_build_runs_repeated_source_regressions_before_edk2_setup` | 890–898 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_boot_args_success_size_cannot_exceed_allocated_capacity` | 900–911 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_stream_descriptor_ownership_is_published_only_before_first_mutation` | 913–924 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_hda_atapi_connection_type_matches_spec_encoding` | 926–928 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_critical_create_event_success_requires_nonnull_handle` | 930–947 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_release_metadata_and_diagnostics_are_consistent` | 949–958 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_release_tree_has_single_consolidated_auxiliary_log` | 960–966 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_release_tree_has_no_cache_or_orphan_temp_artifacts` | 968–984 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_active_source_uses_requested_copyright_and_contact` | 986–998 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_codec_dump_helper_cleans_interrupted_temp_file` | 1000–1008 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_codec_dump_helper_uses_single_deterministic_backup` | 1010–1017 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_build_tooling_uses_one_log_and_pinned_edk2_ref` | 1019–1027 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_codec_dump_requires_pci_attribute_snapshot_before_mmio_enable` | 1029–1038 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_codec_dump_restore_failure_is_not_masked` | 1040–1045 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_corb_rirb_successful_allocatebuffer_requires_nonnull_host` | 1047–1060 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_corb_rirb_cleanup_error_overrides_transport_error` | 1062–1067 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_codec_dump_propagates_controller_errors_and_stops_on_retained_rings` | 1069–1076 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_corb_rirb_dma_ranges_cannot_wrap_physical_address_space` | 1079–1089 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_bdl_dma_range_cannot_wrap_physical_address_space` | 1091–1100 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_codec_dump_statests_mmio_failure_is_not_misclassified_as_no_codecs` | 1102–1111 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_32bit_dma_controllers_validate_full_mapped_ranges_not_only_bases` | 1114–1123 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_codec_dump_preserves_bus_master_containment_for_retained_rings` | 1126–1133 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_codec_dump_stops_same_controller_when_ring_ownership_is_retained` | 1135–1141 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_requested_codec_dump_never_enters_playback_with_retained_ring_ownership` | 1143–1153 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_preboot_key_poll_has_finite_input_drain_budget` | 1155–1161 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_corb_rirb_cleanup_reverifies_dma_disabled_before_freeing_resources` | 1163–1173 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_append_log_has_hard_size_cap_and_recreates_single_file` | 1175–1183 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_file_warning_statuses_are_not_accepted_as_clean_log_io` | 1185–1192 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_config_file_warnings_are_rejected_before_parsing` | 1194–1201 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_debug_init_requires_exact_openvolume_success_and_closes_ambiguous_root` | 1204–1212 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_single_shot_protocol_guard_requires_clean_install_and_valid_handle` | 1214–1223 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_deferred_codec_dump_failure_remains_retryable` | 1225–1234 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_pci_attribute_reads_require_exact_success_and_read_back_mutated_bits` | 1237–1244 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_playback_pci_memory_enable_is_verified_before_mmio` | 1246–1257 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_pci_restore_is_verified_before_ownership_is_cleared` | 1259–1272 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_dma_containment_requires_bus_master_read_back` | 1274–1291 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_event_and_key_notify_ownership_requires_exact_success` | 1293–1313 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_protocol_and_handle_enumeration_reject_warning_statuses` | 1315–1331 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_codec_dump_warning_status_cannot_be_lost_by_efi_error_macro` | 1333–1341 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_mmio_and_location_outputs_require_exact_success` | 1343–1349 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_dma_acquisition_rejects_warning_status_before_publishing_ownership` | 1351–1370 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_dma_release_requires_exact_success_before_clearing_ownership` | 1372–1393 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_pci_flush_warning_is_normalized_before_hardware_progress` | 1395–1401 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRoundTests.test_boot_args_requires_clean_success_before_text_scan` | 1403–1409 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.setUpClass` | 1414–1416 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_failed_create_event_output_is_never_consumed` | 1418–1430 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_failed_file_open_outputs_are_never_closed` | 1432–1445 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_file_close_helper_is_terminal` | 1447–1452 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_file_delete_is_terminal_even_on_delete_warning` | 1454–1464 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_close_event_helper_is_terminal` | 1466–1471 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_pci_unmap_is_never_retried_after_status` | 1473–1482 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_unregister_invalid_parameter_is_terminal_and_undocumented_status_quarantines` | 1484–1488 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_exit_boot_services_callback_has_no_forbidden_boot_service_release_calls` | 1490–1508 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_stall_is_centralized_and_boot_services_safe_during_exit` | 1510–1515 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_keyboard_reads_require_exact_success` | 1517–1519 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_gettime_output_is_used_only_on_exact_success` | 1521–1523 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_controller_dump_root_handle_uses_terminal_close_helper` | 1525–1529 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_build_stages_project_outside_edk2_workspace` | 1531–1536 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_build_restores_temporary_basetools_patch` | 1538–1542 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_build_failure_removes_partial_release_outputs_but_keeps_master_log` | 1544–1550 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound14Tests.test_r16_version_is_consistent_in_active_metadata` | 1552–1559 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound15Tests.setUpClass` | 1564–1566 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound15Tests.test_notification_events_use_callback_tpl_not_application_tpl` | 1568–1576 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound15Tests.test_playback_dma_uses_bus_master_read_not_common_buffer` | 1578–1586 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound15Tests.test_successful_playback_map_ownership_is_independent_of_mapping_token_value` | 1588–1598 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound15Tests.test_exit_boot_services_does_not_replay_codec_or_pci_snapshot` | 1600–1607 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound15Tests.test_exit_boot_services_stream_stop_returns_before_descriptor_restore` | 1609–1615 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound15Tests.test_exit_boot_services_ring_cleanup_does_not_replay_register_snapshot` | 1617–1624 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound15Tests.test_r16_documentation_is_authoritative_and_marks_superseded_rules` | 1626–1638 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound15Tests.test_r16_version_is_consistent_in_all_active_metadata` | 1640–1653 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound16Tests.setUpClass` | 1659–1662 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound16Tests.test_terminal_handoff_uses_before_exit_boot_services_group` | 1664–1672 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound16Tests.test_before_exit_handoff_remains_allocation_and_delayed_work_free` | 1674–1684 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound16Tests.test_single_shot_fails_closed_when_no_guard_can_be_armed` | 1686–1694 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound16Tests.test_r16_metadata_is_exact_and_prior_round_history_is_retained` | 1696–1705 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound17Tests.setUpClass` | 1710–1711 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound17Tests.test_existing_directory_open_uses_zero_attributes_without_create` | 1713–1722 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound17Tests.test_key_dump_worker_is_deferred_to_next_timer_tick` | 1724–1730 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound17Tests.test_successful_deferred_dump_releases_timer_event_in_worker` | 1732–1740 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound17Tests.test_r17_metadata_and_history_are_consistent` | 1742–1752 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound18Tests.setUpClass` | 1757–1758 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound18Tests.test_pci_attribute_rollback_never_replaces_the_entire_mask` | 1760–1770 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound18Tests.test_corb_fallback_owns_temporary_bus_master_enable` | 1772–1790 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound18Tests.test_failed_fallback_bus_master_enable_still_enters_cleanup` | 1792–1799 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound18Tests.test_playback_bus_master_is_delayed_until_final_pre_run_boundary` | 1801–1815 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound18Tests.test_codec_dump_does_not_pre_enable_bus_master` | 1817–1824 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound18Tests.test_r18_metadata_and_history_are_consistent` | 1826–1841 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound19Tests.setUpClass` | 1846–1847 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound19Tests.test_external_pci_restore_can_discharge_fallback_bus_master_ownership` | 1849–1861 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound19Tests.test_codec_dump_restore_manages_fallback_owned_bus_master_bit` | 1863–1872 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound19Tests.test_bus_master_enable_is_refused_while_any_hda_stream_is_running` | 1874–1880 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound19Tests.test_playback_and_corb_check_stream_quiescence_before_enabling_bus_master` | 1882–1891 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound19Tests.test_r19_metadata_and_all_prior_round_history_are_retained` | 1893–1909 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound20Tests.setUpClass` | 1915–1916 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound20Tests.test_fastboot_callback_cannot_preempt_entry_owned_log_state` | 1918–1930 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound20Tests.test_key_request_stays_synchronous_while_entry_is_active` | 1932–1946 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound20Tests.test_key_callback_reentrancy_rearms_timer_until_callback_unwinds` | 1948–1962 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound20Tests.test_r20_metadata_and_history_are_consistent` | 1964–1980 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound21Tests.setUpClass` | 1985–1987 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound21Tests.test_unsupported_common_ring_size_runs_cleanup_after_fallback_bme_enable` | 1989–1995 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound21Tests.test_bus_master_containment_failure_has_error_priority` | 1997–2003 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound21Tests.test_r21_metadata_and_history_are_consistent` | 2005–2019 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound22Tests.setUpClass` | 2024–2026 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound22Tests.test_key_notify_exclusion_is_nesting_safe` | 2028–2040 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound22Tests.test_r22_metadata_and_history_are_consistent` | 2042–2056 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.setUpClass` | 2061–2063 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_success_without_key_notify_handle_is_permanently_contained` | 2065–2079 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_upper_unknown_registration_is_gated_before_known_lower_cleanup` | 2081–2093 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_registration_in_progress_blocks_reentrant_callback_and_worker` | 2095–2118 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_unknown_registration_discards_any_reentrant_pending_request` | 2120–2131 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_deferred_worker_honors_terminal_notify_containment_before_rearm` | 2133–2143 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_codec_dump_helper_enables_release_debug_override_transactionally` | 2145–2198 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_codec_dump_helper_preserves_first_backups_across_repeated_runs` | 2200–2235 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_codec_dump_helper_failure_never_publishes_partial_debug_profile` | 2237–2285 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_codec_dump_helper_does_not_invent_release_profile_when_missing` | 2287–2312 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound23Tests.test_r23_metadata_and_history_are_consistent` | 2314–2328 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound24Tests.setUpClass` | 2333–2335 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound24Tests.test_registration_barrier_is_released_only_after_ownership_decision` | 2337–2363 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound24Tests.test_notify_ownership_mutation_barrier_covers_unregister_transaction` | 2365–2378 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound24Tests.test_notify_mutation_barrier_blocks_key_and_deferred_worker_side_effects` | 2380–2395 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound24Tests.test_primary_compile_command_is_present_executable_and_transactional` | 2397–2409 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound24Tests.test_r24_metadata_and_history_are_consistent` | 2411–2425 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound25Tests.setUpClass` | 2430–2432 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound25Tests.test_undocumented_register_key_notify_status_is_terminally_contained` | 2434–2468 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound25Tests.test_default_edk2_toolchain_is_pinned_to_official_stable_commit` | 2470–2492 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound25Tests.test_r25_metadata_and_history_are_consistent` | 2494–2508 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound26Tests.setUpClass` | 2513–2515 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound26Tests.test_pinned_edk2_workspace_requires_verifiable_git_provenance` | 2517–2530 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound26Tests.test_unpinned_custom_non_git_workspace_keeps_explicit_compatibility_path` | 2532–2541 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `Uefi211ConformanceRound26Tests.test_r26_metadata_and_history_are_consistent` | 2543–2563 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.setUpClass` | 2568–2570 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_controller_reset_requires_idle_rings_and_streams_before_crst_assert` | 2572–2589 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_full_controller_reset_waits_521us_after_observing_crst_one` | 2591–2600 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_immediate_command_timeout_aborts_owned_icb_and_clears_racing_irv` | 2602–2614 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_low_latency_already_running_controller_obeys_codec_discovery_settle` | 2616–2626 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_immediate_and_corb_transports_are_mutually_exclusive` | 2628–2654 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_owned_immediate_command_is_aborted_after_response_poll_read_error` | 2656–2666 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_immediate_command_preserves_ambiguous_entry_state` | 2668–2678 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_owned_immediate_response_is_retired_even_if_response_read_fails` | 2680–2698 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_corb_fallback_never_clears_foreign_w1c_status_on_entry` | 2700–2711 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_corb_snapshot_is_revalidated_before_first_register_mutation` | 2713–2732 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_corb_rirb_rsvdp_fields_are_preserved_on_temporary_writes` | 2734–2760 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_temporary_ring_programming_and_dma_enable_are_read_back_before_dispatch` | 2762–2785 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_corb_memory_error_is_detected_and_reset_only_after_cleanup` | 2787–2804 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_idle_stream_descriptor_is_snapshotted_and_revalidated_before_reset` | 2806–2822 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_bidirectional_output_sets_dir_immediately_after_stream_reset` | 2824–2833 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_stream_lvi_and_format_preserve_reserved_fields` | 2835–2843 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_stream_programming_is_read_back_before_run_and_run_is_confirmed` | 2845–2859 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_stopped_stream_is_reset_before_cbl_restore_and_prior_state_is_restored` | 2861–2875 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_all_nonzero_descriptor_tags_are_reserved_even_when_run_is_clear` | 2877–2884 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_stream_candidate_rejects_reset_or_pending_status` | 2886–2890 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_stream_tag_is_revalidated_immediately_before_descriptor_ownership_and_run` | 2892–2906 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound27Tests.test_r28_metadata_and_history_are_consistent` | 2908–2932 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound28Tests.setUpClass` | 2937–2939 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound28Tests.test_32bit_only_controller_never_writes_read_only_ring_upper_bases` | 2941–2956 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound28Tests.test_32bit_only_controller_never_writes_read_only_stream_bdl_upper_base` | 2958–2971 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound28Tests.test_stream_tag_scan_rejects_concurrent_descriptor_mutation` | 2973–2979 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound28Tests.test_stream_restore_is_read_back_before_descriptor_ownership_is_released` | 2981–3002 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound29Tests.setUpClass` | 3008–3010 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound29Tests.test_start_revalidates_complete_descriptor_after_foreign_tag_scan` | 3012–3020 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound29Tests.test_local_descriptor_restore_failure_never_escalates_to_global_crst` | 3022–3031 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound29Tests.test_cleanup_degradation_keeps_real_status_without_forcing_replay` | 3033–3041 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound29Tests.test_stream_descriptor_kind_is_owned_and_cleared_with_descriptor` | 3043–3048 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound29Tests.test_async_cleanup_retains_real_cleanup_status_for_retry` | 3050–3054 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound29Tests.test_generic_uefi_warning_code_is_not_misused_for_mmio_cleanup` | 3056–3058 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound29Tests.test_fastboot_surfaces_stream_restore_degradation_before_context_release` | 3060–3067 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `ReleaseAuditRound30Tests.setUpClass` | 3073–3075 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `ReleaseAuditRound30Tests.test_packaging_uses_the_same_path_resolved_zip_tools_as_preflight` | 3077–3083 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `ReleaseAuditRound30Tests.test_r30_release_metadata_is_synchronized_and_history_is_preserved` | 3085–3106 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `ReleaseAuditRound31Tests.setUpClass` | 3111–3113 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `ReleaseAuditRound31Tests.test_source_hygiene_is_gated_before_python_test_imports` | 3115–3126 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `ReleaseAuditRound31Tests.test_append_log_recreates_file_at_the_exact_size_limit` | 3128–3131 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `ReleaseAuditRound31Tests.test_r31_release_metadata_is_synchronized_and_r30_history_is_preserved` | 3133–3153 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.setUpClass` | 3158–3161 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_connection_list_rejects_sequential_range_entries` | 3163–3167 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_command_transports_reject_unsupported_indirect_nids` | 3169–3175 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_generator_rejects_indirect_configdata_nid_encoding` | 3177–3188 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_applealc_profile_fallback_skips_pin_when_configdefault_read_fails` | 3190–3195 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_controller_dump_propagates_mandatory_register_read_failures` | 3197–3204 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_volume_knob_widget_type_is_defined_and_named` | 3206–3209 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_widget_indexed_state_matches_direct_nid_domain` | 3211–3217 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_selected_route_classification_fails_closed_on_config_read_error` | 3219–3230 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_route_candidate_reuses_primary_classification_and_fallback_fails_closed` | 3233–3241 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_output_pin_programming_propagates_configdefault_read_failure` | 3243–3249 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_realtek_profile_fails_closed_when_configdefault_read_fails` | 3251–3253 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_legacy_realtek_fanout_skips_pins_when_configdefault_read_fails` | 3255–3258 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_full_applealc_route_does_not_bypass_digital_policy_on_config_read_error` | 3261–3264 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_path_search_validates_direct_nids_before_indexing_reduced_arrays` | 3267–3272 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_selector_claim_tables_validate_nids_before_128_entry_indexing` | 3275–3279 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_configdata_runtime_rejects_indirect_nids_before_snapshot_indexing` | 3282–3288 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound32Tests.test_r32_release_metadata_is_synchronized_and_r31_history_is_preserved` | 3291–3311 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.setUpClass` | 3316–3317 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.test_r33_release_metadata_is_synchronized_and_r32_history_is_preserved` | 3319–3341 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.test_quick_path_nonforced_configdefault_read_fails_closed` | 3343–3351 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.test_alcid_safe_fanout_configdefault_read_fails_closed` | 3353–3359 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.test_realtek_applealc_combo_configdefault_read_fails_closed` | 3361–3367 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.test_codec_dump_reads_subsystem_id_from_function_group_not_root` | 3370–3379 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.test_codec_dump_subsystem_id_is_limited_to_spec_function_group_types` | 3381–3387 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.test_codec_dump_propagates_mandatory_identity_read_failures` | 3389–3401 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.test_embedded_efi_validator_accepts_boot_service_driver_and_rejects_wrong_subsystem` | 3403–3449 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `HdaSpecConformanceRound33Tests.test_build_validates_pe32plus_x64_efi_driver_before_copying_outputs` | 3451–3462 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.setUpClass` | 3467–3470 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests._validator_script` | 3473–3478 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests._minimal_efi_image` | 3481–3509 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests._run_validator` | 3511–3519 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_exact_profile_requires_successfully_read_revision_and_subsystem_identity` | 3521–3544 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_exact_profile_snapshot_uses_same_identity_validity_gate` | 3546–3555 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_pin_priority_fails_closed_before_parsing_or_pin_sense_when_config_default_read_fails` | 3557–3568 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_release_validator_rejects_zero_sections_and_out_of_file_section_raw_data` | 3570–3585 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_release_validator_requires_entry_point_in_section_and_directory_count_to_fit_optional_header` | 3587–3600 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_release_validator_walks_aligned_win_certificate_entries` | 3602–3623 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_parameter_transport_failures_cannot_masquerade_as_zero_capabilities_in_dac_selection` | 3626–3635 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_mutating_path_programming_propagates_required_capability_read_errors` | 3637–3649 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_snapshot_does_not_classify_failed_widget_caps_as_audio_output` | 3651–3657 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_fanout_selector_safety_fails_closed_when_widget_caps_are_unreadable` | 3659–3675 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_amp_capability_transport_errors_are_propagated_before_gain_programming` | 3677–3691 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_route_classification_rejects_unreadable_pin_capabilities` | 3693–3700 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_snapshot_node_is_published_only_after_complete_capture_and_pin_caps_are_known` | 3702–3712 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_warm_reset_snapshot_aborts_on_unreadable_widget_caps` | 3714–3719 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound34Tests.test_transactional_snapshot_never_skips_required_eapd_or_input_amp_state_on_read_failure` | 3721–3731 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests.setUpClass` | 3736–3737 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests._validator_script` | 3740–3745 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests._minimal_efi_image` | 3748–3775 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests._run_validator` | 3777–3785 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests.test_release_validator_rejects_certificate_table_overlapping_section_raw_data` | 3787–3803 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests.test_release_validator_rejects_section_raw_data_overlapping_headers_or_another_section` | 3806–3829 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests.test_release_validator_requires_executable_image_characteristic` | 3832–3840 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests.test_release_validator_rejects_invalid_file_and_section_alignment` | 3842–3857 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests.test_fallback_candidate_classification_rejects_unreadable_pin_capabilities` | 3859–3871 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests.test_input_amp_route_prefilter_fails_closed_when_widget_caps_are_unreadable` | 3873–3880 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests.test_selected_route_classification_enforces_digital_policy_from_live_pin_caps` | 3884–3896 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound35Tests.test_analog_path_programming_refuses_live_digital_pin_before_first_mutation` | 3899–3913 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound37Tests.setUpClass` | 3918–3919 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound37Tests._validator_script` | 3922–3927 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound37Tests.test_release_validator_accepts_valid_subpage_edk2_xcode5_alignment` | 3929–3969 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound37Tests.test_release_validator_rejects_subpage_alignment_when_file_offset_differs_from_rva` | 3971–4010 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound37Tests.test_r37_release_metadata_is_synchronized_and_r36_history_is_preserved` | 4012–4026 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound36Tests.setUpClass` | 4033–4034 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_audit_regressions.py` | `FinalAuditRound36Tests.test_build_scrubs_transient_macos_metadata_before_hygiene_and_each_regression_round` | 4036–4051 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound36Tests.test_hygiene_gate_still_rejects_macos_metadata_if_cleanup_does_not_remove_it` | 4053–4058 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_audit_regressions.py` | `FinalAuditRound36Tests.test_r36_release_metadata_is_synchronized_and_r35_history_is_preserved` | 4060–4074 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_forcepin_runtime.py` | `definition` | 18–32 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_forcepin_runtime.py` | `ForcePinRuntimeTests.setUpClass` | 96–100 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_forcepin_runtime.py` | `ForcePinRuntimeTests.compile_run` | 102–110 | I | AST / uso em testes; revisão individual pendente |
| `Tests/test_forcepin_runtime.py` | `ForcePinRuntimeTests.test_applealc_runtime_path_does_not_override_forced_pin` | 112–130 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_forcepin_runtime.py` | `ForcePinRuntimeTests.test_route_fallback_does_not_override_forced_pin` | 132–156 | E | unittest OK; cobertura de branches não demonstrada |
| `Tests/test_forcepin_runtime.py` | `FanoutSnapshotInvariantTests.test_legacy_fanout_captures_all_touched_routes_before_programming` | 159–167 | E | unittest OK; cobertura de branches não demonstrada |
### Checkpoint persistente R39 e bloqueios de Release
- **PATCH único (nome histórico preservado para manter gate de higiene):** `Audit/diffs/R37-to-R38.patch` agora contém o diff **cumulativo R37→R39**, não apenas R38. SHA-256 `f79c9f8fcf678844453be85c8a9e3b7f52367770a4ede34ac8e398963dcce435`. Reproduz exatamente os quatro arquivos de código/testes e mantém históricos, versões e permissões originais. NÃO renomear sem atualizar o gate legado que valida precisamente esse único caminho.
- **Driver C inalterado R38→R39:** SHA-256 `2537228ca89640673f9b349bf170774b79ea3fd9f9c8a76ad4895b9e8966f96e`. Para rollback/continuidades, conferir este valor antes de qualquer edição.
- **Gerador corrigido:** SHA-256 `a2d672acd06a2780f8ba37f3d4c32f46d6b87f872944db0c39e8bf09d53ee70f`. **Teste R39:** SHA-256 `7dc7a512978925ccf4ee83417c1646e8068f18058f3849463af14a3bfacfd7d2`.
- **Log gate consolidado:** `Release/Logs/BUILD-COMPILE.log` SHA-256 `84fda8ed9addfcdc79c5a74f1d3dfac6bb9f6a29a82d7d6910dd4b6094fa5db4`; contém 5 linhas de rodada, 5 resultados `Ran 271 tests`, 5 `OK`, erro final EDK2. Código de saída global 1 **por EDK2 ausente**, não por falha nos 271 casos.
- **Próximo ponto exato:** em macOS com EDK2 fixado no ref e commit configurados e verificáveis, executar `AUTO_FETCH_EDK2=0 SOURCE_TEST_ROUNDS=5 ./Build-HDAUniversalBootChimeDxe.command` em cópia limpa; revalidar binários DEBUG/RELEASE PE32+ X64 EFI Boot Services Driver; executar boot HDA real com cold/warm, ForcePin, FullPathMap, fallback, EAPD, PCM e rollback em ExitBootServices; continuar revisão manual das funções marcadas `I`/`AR38` que não receberam análise dirigida nova. Preservar o único log, relatório e patch.
- **RELEASE não aprovado:** o ambiente atual não possui EDK2 nem controlador HDA físico. Não há evidência de compilação EFI, ausência universal de race/memory leak ou cobertura funcional de 100%. O relatório R38 histórico e a matriz R39 atualizada deixam essas pendências expressas; a rodada R39 de correção de gerador e entrega de source encerra com evidências de host, sem indevida certificação de Release.


### Verificação independente do ZIP distribuído (extração Unix)
- **Arquivo de entrega:** `HDAUniversalBootChimeDxe-v1.0.82-R39-CORRECTED-FULL-SOURCE.zip`, 24 membros. Todos os CRCs e a integridade ZIP foram confirmados, com nomes de entradas únicos.
- **Permissões de distribuição:** os metadados Unix de cada membro ZIP correspondem aos modos do source de origem. O utilitário CLI `unzip -q` restaurou os mesmos modos e os mesmos hashes SHA-256 dos **24/24** arquivos após a extração, incluindo o comando de compilação com modo `0755`.
- **Regressão após extração real:** a suíte integral, executada *na pasta descompactada pelo `unzip`* com `PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s Tests -p 'test*.py' -q`, passou **271/271**.
- **Controle de falso positivo do método:** uma tentativa separada com a API Python `zipfile.extractall()` perdeu os bits executáveis na extração, o que fez o teste de permissão do build falhar (1/271). O ZIP já possuía os bits corretos (`0755`). A reprodução CLI comprovou que se tratava de comportamento do extrator, não de falha do código ou de empacotamento; os scripts e o teste **não foram modificados** para contornar esse resultado. Recomenda-se `unzip` ou outro extrator que preserve permissões Unix.
- **Limites persistentes:** testes em host não comprovam firmware real; ausência de EDK2 e hardware HDA permanece impeditiva para declarar Release validado, cobertura integral funcional ou ausência universal de falhas.


---

## ADENDO R40 — auditoria independente do gate PE/COFF, inventário e checkpoints (19/10/2026)
**Status corrente:** correção do validador de Release validada em host; **NÃO é um Release EFI compilado, assinado ou testado em hardware.** Não existe evidência para 100% de cobertura funcional do driver. Este adendo prevalece sobre contagens/etapas históricas R38–R39, que permanecem intactas como trilha de auditoria.
### Identidade, origem e política de alterações
- Base exclusiva desta rodada: `HDAUniversalBootChimeDxe-v1.0.82-R39-CORRECTED-FULL-SOURCE.zip` SHA-256 `b8559e258c217acf3df577e432dbbac6fd3133027ba1313170b92fe30c47422b`, recebida como último source validado da conversa. ZIP da base R39 íntegro, 24 arquivos; pacotes R37/R38 consultados apenas para verificação diferencial e reprodução do patch.
- Nenhum commit Git verificável do projeto dentro do ZIP. A área Git local usada para gerar o diff foi uma **cópia sintética de R37**, não o HEAD remoto. O EDK2 configurado no script refere `edk2-stable202608`/`2970e5699ba6267f3384ffab20f96647578aebc8`; **não foi localizado nem autenticado checkout EDK2 neste host**.
- Arquivos de implementação alterados na R40: `Build-HDAUniversalBootChimeDxe.command` (validador PE/COFF). Testes alterados: `Tests/test_audit_regressions.py` (5 regressões e correção de Characteristics em um fixture historicamente não executável). O patch, relatório e log são metadados de entrega. **Código C, tabelas AppleALC, PCM, MP3, INI, INF/DSC, README e CHANGELOG: bytes preservados desde R39.**
- Versão do produto `1.0.82-final-audit` não foi alterada; R40 nomeia a rodada de auditoria, não uma nova versão comercial.
### Defeitos reproduzidos, causas raízes e correção mínima
O validador PE/COFF executado pelo build aceitava invariantes insuficientes nos headers de seção e no ponto de entrada. O risco comprovado é **aceitar artefatos malformados na etapa de publicação**; não equivale a demonstrar execução incorreta do driver ou exploit em firmware.
| Caso | Falha antes | Alteração de origem | Regressão pós-correção |
|---|---|---|---|
| Entry RVA em seção sem execução | `entry_mapped` checava apenas intervalo de RVA | requer bit `IMAGE_SCN_MEM_EXECUTE=0x20000000` | rejeitado |
| Entry RVA em cauda virtual sem bytes no arquivo | mapeamento aceitava `max(VirtualSize,SizeOfRawData)` inteiro | exige entrypoint dentro dos `SizeOfRawData` realmente gravados | rejeitado |
| Spans de seções virtuais sobrepostos | ausência de ordem/fim virtual anterior | `previous_virtual_end` e rejeição de interseção | rejeitado |
| RVAs de seção em ordem decrescente | tabela aceita sem testar monotonicidade | mesma verificação de `previous_virtual_end` | rejeitado |
| Dados raw de seções fora da ordem da tabela de RVAs | overlap raw era testado, mas ordenação não | `previous_raw_offset` crescente para seções raw | rejeitado |

**Controle de regressão:** cada cenário foi construído como PE/COFF sintético e mostrado inicialmente aceito incorretamente pelo validador antigo (os quatro primeiros com execução pré-fix dos testes; quinto com reprodução independente pré-fix), depois rejeitado pela função real corrigida. As fixtures válidas de PE32+ X64 e do alinhamento XCODE5 sub-page permanecem aprovadas. Uma fixture `.text` existente recebeu `Characteristics=0x60000020` para expressar corretamente que contém código executável; nenhuma asserção anterior foi removida.
### Rodadas reais e resultados verificáveis
| Sequência | Método / cenário | Resultado real | Limitação |
|---|---|---|---|
| Baseline | `python3 -B -m unittest discover -s Tests -p "test*.py" -q` extraído do R39 | **271/271** | host, sem hardware |
| Nova investigação do validador | cinco entradas malformadas reais passadas ao Python embarcado no `.command` | inicialmente aceitas indevidamente; novas regressões falhavam no código anterior | provas dirigidas, PE sintético |
| Correção | mudança no validador, fixture válida e cinco testes adicionados | **276/276** em execução unitária completa | host |
| Auditoria de regressão | `bash -n` scripts e AST Python; `git apply --check` sobre R37 | aprovado | não prova compilação EFI |
| Diff independente | `git apply` cumulativo R37→R40 sobre árvore R37 limpa; comparação dos cinco arquivos versionados cobertos pelo diff | idênticos byte a byte ao source R40 | não é repositório upstream |
| Pipeline final isolado | `AUTO_FETCH_EDK2=0 SOURCE_TEST_ROUNDS=5 ./Build-HDAUniversalBootChimeDxe.command` | **5/5 × 276/276 = 1.380 aprovações**; erro final somente EDK2 ausente | testes de fonte, não compilação EFI |
| Hardware & firmware | DMA HDA real, Boot Services, EBS, cold/warm boot e playback | **NÃO EXECUTADO** | bloqueia aprovação Release |
O log mestre `Release/Logs/BUILD-COMPILE.log` registra cinco linhas `Ran 276 tests` e cinco `OK`; na sequência registra `ERROR: EDK2 WORKSPACE not found and AUTO_FETCH_EDK2=0 ...`. Não interpretar o `exit 1` global como falha de teste; tampouco declarar sucesso do build EFI.
### Inventário consolidado de todos os 24 arquivos da entrega
**Legenda de cobertura:** `I` = arquivo inventariado/lido para identificação e integridade; `M` = inspeção manual direta com escopo descrito; `T` = teste executado, sem presumir cobertura de branches; `H` = hardware real. Nenhuma linha abaixo marcada `H`. Hashes são dos **bytes efetivos do arquivo final**, exceto o `.md` em que o hash próprio no mesmo conteúdo seria autorreferente. `M` não significa automaticamente auditoria integral de todas as rotinas.
| Caminho | Linhas / tamanho | Modo | SHA-256 | Papel / evidência de revisão |
|---|---:|---:|---|---|
| `Audit/R38-AUDIT.md` | 1359 linhas | `0644` | autorreferente; verificar SHA externo do ZIP | relatório cumulativo R38–R40; histórico, matrizes, checkpoints; I; R40: M adendo; documentação interna |
| `Audit/diffs/R37-to-R38.patch` | 434 linhas | `0644` | `1dc98235b1562b5de2a6a92fd0cbef800f2f5862bd8af78f92acb3dd3629bb2a` | patch cumulativo R37→R40; nome histórico exigido pelo gate; I/M/T: git apply --check, apply + comparação byte a byte |
| `Build-HDAUniversalBootChimeDxe.command` | 913 linhas | `0755` | `5aac8c90a15f72b9f8a3bad05cbd68dec568350427e4d3c44d4010f217849404` | build, higiene, descoberta EDK2 e validador PE/COFF; I/M/T: leitura manual 1–913, PE 626–823, 5 testes novos |
| `CHANGELOG.md` | 417 linhas | `0644` | `2d26c0aa4ca11bf02c124eca3d06dc970d7e8a53a35db070b9fe7f855dbe7ad3` | histórico de versões do produto, não alterado; I; conteúdo herdado, sem revisão funcional exaustiva |
| `Enable-HDAUCodecDump-NextBoot.command` | 243 linhas | `0755` | `c7f7cd4706733cd7696832b654a5c4431e700745036b3f747fa01e4ef336747a` | helper de coleta de dump e rollback temporário; I/M: leitura manual 1–243; não exercitado em macOS |
| `HDAUChimeDebug.ini` | 59 linhas | `0644` | `fa7698619bf07ba1840ccc67c676ac3df2447ee318ddad6a85e4df36b76da246` | configuração Debug; I/M: leitura manual 1–59; sem boot |
| `HDAUChimeRelease.ini` | 59 linhas | `0644` | `901561bc324f520002c82d5d02e3b29cc0f3ee44a0baf0283397fe3b66ee57e7` | configuração Release; I/M: leitura manual 1–59; sem boot |
| `HDAUniversalBootChimeDxe/BootChimePcm.h` | 20293 linhas | `0644` | `68220bf126b43627375ba27b6ef0cdbe7f8ceaa96b8ceb7c57b45c483e6a5c43` | PCM embutido; ativo de áudio; I/T: dimensões/invariantes em testes; bytes preservados |
| `HDAUniversalBootChimeDxe/BootChimePcm44.h` | 18646 linhas | `0644` | `1da445f511019536d2397373b5c4fb24dbe1ff38fe0ebdfafda039d030331d04` | PCM alternativo; ativo de áudio; I/T: dimensões/invariantes em testes; bytes preservados |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` | 10894 linhas | `0644` | `2537228ca89640673f9b349bf170774b79ea3fd9f9c8a76ad4895b9e8966f96e` | driver DXE, HDA MMIO, DMA, playback e callbacks; I/M dirigida + T host; 212 definições C; SEM 100% funcional |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf` | 45 linhas | `0644` | `222f8f58ce0e519069bc2ee35b35848f141922cf725ffe334bebb1305012bff3` | configuração do módulo EDK2; I/M: leitura manual 1–45; build EDK2 pendente |
| `HDAUniversalBootChimeDxe/HdaAppleAlcLayouts.h` | 3672 linhas | `0644` | `bd348bd45382a3b3bfa5902f2b56ca088b23a0418bf6d157a60046c72dd560a1` | tabelas de perfis geradas; I/T: offsets/invariantes automáticos; sem auditoria manual integral |
| `HDAUniversalBootChimeDxe/HdaUefi.h` | 577 linhas | `0644` | `b6357b966e78babecfa497711851c099409a51192e0f38e11f917e1b5ec578f5` | tipos, macros, HDA/UEFI definitions; I/M: leitura manual dirigida 1–577; uso dependente de hardware |
| `HDAUniversalBootChimeDxePkg.dsc` | 33 linhas | `0644` | `5ada46b1cb0eba180ce068d45d4ca54e96ba5ad4f7c39b180a8b5f236589a174` | configuração do pacote EDK2; I/M: leitura manual 1–33; build pendente |
| `README.md` | 608 linhas | `0644` | `f3d9a12ab86c078a7eb712a6e982f1977fb5e399ea83a30b60fffc643d388257` | documentação histórica do produto; I; preservado; sem revisão funcional exaustiva |
| `Release/Logs/BUILD-COMPILE.log` | 1425 linhas | `0644` | `a41adf8af9ce3221dd939553017167c262953cc5f781e58954f8adcbde99cbaf` | log mestre único do gate R40; I/T: 5×276=1380; EDK2 indisponível |
| `Setup-EDK2.command` | 85 linhas | `0755` | `6a515d741b0ef47cff696a08d1b9aca4f5aae2ad558c9dfd41674994e56318c4` | preparo workspace e toolchain; I/M: leitura manual 1–85; não instalado aqui |
| `Tests/test_audit_regressions.py` | 4159 linhas | `0755` | `b3dd707500bd66546575538b340302d6fa8200740275233a95cfdd2395fab5ef` | regressões de source, novo gate PE/COFF R40; I/M dirigida / T: 273 casos nesse módulo; sem auditoria manual de cada teste |
| `Tests/test_forcepin_runtime.py` | 171 linhas | `0644` | `00d77fb92e43e0b97e50ad7eeb5406613421ffa3dc0aad1a22cfbef94cba16fb` | harness C ForcePin e snapshots herdados R38; I/T: 3 regressões passadas; dados C simulados |
| `Tools/generate_applealc_profiles.py` | 765 linhas | `0755` | `a2d672acd06a2780f8ba37f3d4c32f46d6b87f872944db0c39e8bf09d53ee70f` | gerador e parser ZIP/AppleALC; correção R39 preservada; I/M: leitura manual 1–765; T: gerador coberto por regressões |
| `assets/BootChime.mp3` | 30.407 bytes | `0644` | `7914bd202b1f539d1d8aafb7a371ee8008936bc5323094e52ca6439cf411e3ad` | MP3 original; I: hash+bytes preservados; não testado em codec físico |
| `docs/NO-SHELL-EFI-TEST.md` | 53 linhas | `0644` | `892dbf1611ea71e258c234453a8942539b1a7efc8afda61f4c8eded5c1b2d5e8` | instruções de teste EFI, documento histórico; I; não equivale a teste realizado |
| `docs/PROJECT-REPORT.txt` | 370 linhas | `0644` | `24c7735d7506a1dbf1c290165faf54eb949a484a25d54399439f01c2b6ea543c` | documentação TXT única do pacote; I; sem reescrita para preservar histórico |
| `docs/TESTING.md` | 81 linhas | `0644` | `4adf5045d95beeb66d03fdd9749ff0d744ae3430293042f9b0dd1bf1e43a6b40` | roteiro de testes, histórico; I; não substitui teste de hardware |

### Matriz complementar de revisão por função — implementação C
A matriz R39 anterior enumera **212 definições de funções C**; C foi comparado byte a byte com R39, portanto seus intervalos de linhas permanecem válidos. **Esta rodada NÃO revisou manualmente todas as 212 funções.** As rotinas abaixo tiveram inspeção direta dirigida; todo o restante mantém os status históricos da matriz anterior e continua pendente de auditoria funcional integral quando indicado.
| Função / grupo | Linhas C | Nível | Parâmetros/retornos, riscos e evidências dirigidas |
|---|---|---|---|
| `HdaF6KeyNotify` | 232–279 | M/T (direta; sem teste EFI) | Callback TPL, reentrada, owner flags, deferred dump |
| `HdaAsciiLower / HdaAsciiEqualsInsensitive` | 226–290 | M | Comparação de ASCII case-insensitive |
| `HdaSkipSpaces / HdaTrimRight` | 292–307 | M | Trimming e limites de strings |
| `HdaParseNumberStrict / HdaParseBoolStrict` | 309–364 | M | Parsing numérico/booleano com falhas de entrada |
| `HdaApplyConfigBool / HdaAsciiToUnicode / HdaSetNvramAscii` | 366–394 | M | Escrita de configurações e NVRAM; sem boot físico |
| `HdaParseConfigBuffer / HdaReadConfigFromPath` | 751–841 | M/T dirigida | Buffer INI, overflow e I/O; sem emulação filesystem UEFI |
| `HdaFindAlcIdInAscii / HdaBootArgIsEnabledToken` | 1015–1086 | M/T dirigida | Parsing e tokenização de argumentos de boot |
| `HdaFindCodecDumpRequestInAscii / HdaConvertUtf16BootArgsInPlace` | 1088–1128 | M/T dirigida | Entrada Unicode e dump request |
| `HdaReadAlcIdFromBootArgs` | 1130–1224 | M/T dirigida | Consulta a GetVariable e buffers/capacidade |
| `HdaConfigVerbIsRollbackSafe / HdaConfigSpanIsRollbackSafe` | 3679–3754 | M/T dirigida | Classificação de verbs com restauração |
| `HdaApplyAlcIdPrebootVerbs / HdaApplyExactProfileVerbs` | 3756–3860 | M/T dirigida | Mutação de codec com snapshots e propagação de falhas |
| `HdaGetSubnodes / HdaGetConnections` | 3932–4042 | M/T dirigida | NIDs e lista de conexões; hardware simulado no histórico |
| `HdaPathSearch` | 4535–4621 | M/T dirigida | DFS/BFS de rota e limite de nós, condições de erro |
| `HdaIsAudioOutputDac / HdaSelectPcmFormat` | 7165–7253 | M/T dirigida | Classificação DAC e formato PCM |
| `HdaCopyBootChime / HdaAllocatePlaybackBuffers` | 8450–8703 | M/T dirigida | DMA buffers e cópia de PCM; depende de firmware |
| `HdaFreePlaybackBuffers / HdaFinalizePlaybackContext` | 8705–8828 | M/T dirigida | Unmap, ownership e limpeza; comportamento físico não verificado |
**Limites desta matriz:** leitura/inspeção não substitui simulação intercalada de callbacks TPL, cobertura de error paths DMA em hardware, uso real de PCI I/O, ExitBootServices, stresses prolongados ou ferramenta de profiling. Nenhum problema adicional no C foi provado a ponto de justificar mutação do driver na R40.
### Matriz por função — pipeline de compilação e gate
O `Build-HDAUniversalBootChimeDxe.command` foi inspecionado manualmente em sua extensão (913 linhas). As funções shell e o Python embarcado não são declarados como exaustivamente testados em todos os ramos: apenas `validate_efi_image` recebeu novos testes de execução reais.
| Função do `.command` | Linhas | Status | Dependências/riscos conferidos |
|---|---|---|---|
| `restore_basetools_wrapper` | 52–61 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `cleanup_build_transaction` | 63–81 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `lowercase` | 88–90 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `cpu_count` | 92–106 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `require_command` | 108–111 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `clean_transient_source_metadata` | 113–130 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `verify_source_tree_hygiene` | 132–145 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `preflight_tools` | 147–164 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `run_source_regressions` | 166–197 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `ensure_basetools_c_binaries` | 199–211 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `patch_basetools_shell_wrappers` | 213–245 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `find_edk2_workspace` | 247–268 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `bootstrap_edk2_if_needed` | 270–306 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `verify_edk2_ref` | 308–333 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `prepare_edk2` | 335–383 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `find_unique_file` | 385–405 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `stage_package_for_build` | 407–430 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `resolve_edk2_library_paths` | 432–538 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `select_toolchain` | 540–550 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `files_are_identical` | 552–568 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `find_output` | 570–624 | M — fluxo e dependências shell; testes indiretos quando existentes |
| `validate_efi_image` | 626–823 | M/T — gate PE/COFF, cinco negativos e positivos antigos |
| `build_one` | 825–859 | M — fluxo e dependências shell; testes indiretos quando existentes |
Outras funções shell inspecionadas: `Setup-EDK2.command` → `cpu_count` (15–33); `Enable-HDAUCodecDump-NextBoot.command` → `is_boot_efi_root` (14–17), `has_installed_driver` (19–23), `find_efi_root` (25–66), `cleanup_tmp` (109–118), `set_key_file` (126–153), `set_key` (155–157). Estes utilitários não foram executados em um Mac durante esta rodada.
### Matriz por função — novo módulo de teste R40
| Classe/função | Linhas | Status | Evidência |
|---|---|---|---|
| `validate_efi_image (shell + Python embarcado)` | 626–823 | M/T | Inspeção dos campos COFF, cabeçalhos, seções, entrypoint, certificados; 5 novas regressões antes/depois |
| `ReleaseGateRound40Tests.setUpClass` | 4082–4085 | M/T | Lê validador real do source antes de executar os casos |
| `ReleaseGateRound40Tests._run_validator` | 4087–4088 | M/T | Delega execução isolada do validador real |
| `ReleaseGateRound40Tests._base` | 4090–4091 | M/T | Cria PE de base de fixture histórico |
| `test_rejects_entry_in_nonexecutable_section` | 4093–4098 | M/T | Permissão IMAGE_SCN_MEM_EXECUTE ausente |
| `test_rejects_entry_in_zero_filled_unbacked_section_tail` | 4100–4107 | M/T | Entry RVA aponta somente para cauda virtual não gravada |
| `test_rejects_overlapping_virtual_sections` | 4109–4124 | M/T | Dois spans virtuais sobrepostos |
| `test_rejects_descending_virtual_sections` | 4126–4142 | M/T | Tabela de seções ordenada com RVAs decrescentes |
| `test_rejects_reversed_raw_section_order` | 4144–4160 | M/T | Section raw offsets fora da ordem dos RVAs |
| `HdaSpecConformanceRound33Tests.test_embedded_efi_validator_accepts_boot_service_driver_and_rejects_wrong_subsystem` | contexto 3390–3432 | M/T | Fixture .text marcado executável; não altera a finalidade do teste |
O restante da matriz AST Python anterior permanece aplicável aos módulos preexistentes: os cinco casos R40 foram acrescentados no final do arquivo de testes (nenhuma função existente removida). Revisão detalhada manual de todas as funções de teste não foi declarada.
### Checkpoint persistente R40, preservação e ponto de retomada
- **Driver imutável vs R39:** `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` SHA-256 `2537228ca89640673f9b349bf170774b79ea3fd9f9c8a76ad4895b9e8966f96e`.
- **Build corrigido:** SHA-256 `5aac8c90a15f72b9f8a3bad05cbd68dec568350427e4d3c44d4010f217849404`; **teste atualizado** SHA-256 `b3dd707500bd66546575538b340302d6fa8200740275233a95cfdd2395fab5ef`.
- **Patch único cumulativo:** caminho histórico `Audit/diffs/R37-to-R38.patch`, conteúdo agora R37→R40, SHA-256 `1dc98235b1562b5de2a6a92fd0cbef800f2f5862bd8af78f92acb3dd3629bb2a`. Sua reaplicação e comparação foram verificadas. É o único `.patch` no projeto; não criar diff por rodada.
- **Log único de build:** `Release/Logs/BUILD-COMPILE.log` SHA-256 `a41adf8af9ce3221dd939553017167c262953cc5f781e58954f8adcbde99cbaf`. Histórico R39 preservado no pacote R39 anterior, não duplicado na árvore R40.
- **Matrizes anteriores:** índices de 212 funções C (sem modificações em C) + AST Python histórico estão neste mesmo relatório; este adendo acrescenta os novos testes e as funções shell, distinguindo revisão e inventário.
- **Próximo ponto exato (pendências de Release):** extrair o ZIP R40 com `unzip`/extrator que preserve bits executáveis; conferir todos os SHA-256; executar novamente `SOURCE_TEST_ROUNDS=5 AUTO_FETCH_EDK2=0 ./Build-HDAUniversalBootChimeDxe.command` **em host com EDK2 no commit fixado**; validar PE32+ X64 de DEBUG/RELEASE realmente produzido; ensaiar cold/warm boot em HDA real, saída de áudio, múltiplos codecs, ForcePin, rollback, TPL/ExitBootServices, travamentos e reentrância; completar manualmente todas as funções C ainda sem revisão dirigida na matriz (não inferir 100% dos 276 testes); acrescentar evidências futuras neste **mesmo `.md`, `.patch` e `.log`**, sem apagar histórico.
- **Release:** **BLOQUEADO**. Sem `.efi` criado/inspecionado neste host, sem EDK2 instalado e sem validação HDA física, ainda existem riscos de concorrência, DMA/cleanup, memory/resource leaks e incompatibilidade não eliminados. Não afirmar 100% nem binário pronto para distribuição.


---

## ADENDO R41 — correção dirigida, verificação de código real e checkpoint de distribuição (19/10/2026)

**Conclusão delimitada:** source R41 corrigido, com duas falhas reproduzidas em gerador/gate; testes de host aprovados. **NÃO há evidência suficiente de 100% de análise efetiva de todas as funções, nem de compilação EFI ou aprovação em hardware.** O histórico de adendos anteriores permanece acima; esta seção estabelece o estado mais recente.

### Base, proveniência e fronteira exata da alteração

- Única base de produção usada: `HDAUniversalBootChimeDxe-v1.0.82-R40-CORRECTED-FULL-SOURCE.zip`, SHA-256 `e898acc169edc184ba27d01c36f70660528ec25fb9967ee61a19c8abef177c11`; testada inicialmente com 276/276; 24 arquivos. A R37 foi consultada **somente** para reproduzir patch cumulativo, jamais substituindo o código de produção R40.
- ZIP de entrada **sem `.git` ou commits originais**: commits criados localmente para o diff representam apenas uma base sintética R37 e não são prova de histórico upstream. O checkout EDK2 pinado não estava disponível neste host.
- Modificações de implementação R40→R41, exclusivamente: `Tools/generate_applealc_profiles.py` (validação de identidades opcionais) e `Build-HDAUniversalBootChimeDxe.command` (proteção de cabeçalhos PE). Testes novos acrescentados ao fim de `Tests/test_audit_regressions.py` (duas funções AppleALC + uma função PE). Nenhum teste anterior foi excluído. Arquivos de relatório/patch/log atualizados exclusivamente por necessidade da entrega.
- **Sem alteração C em R41**: `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` SHA-256 `2537228ca89640673f9b349bf170774b79ea3fd9f9c8a76ad4895b9e8966f96e` = R40. PCM 44/48 kHz, áudio MP3, layouts gerados, .ini, INF, DSC, README, CHANGELOG e scripts de instalação também permanecem byte a byte como no R40.
- Versão comercial 1.0.82 preservada. R41 é designação **do checkpoint de auditoria**, não nova funcionalidade nem nova versão do driver.

### Defeitos comprovados, reprodução, causa raiz e correções mínimas

| Falha demonstrada antes | Causa raiz técnica | Correção na implementação de origem | Evidência depois |
|---|---|---|---|
| Perfis `HDAConfigDefault` com `RevisionID`, `RevisionMask`, `SubsystemID`, `SubsystemMask` malformados ou fora de 32 bits podiam ser aceitos como wildcard ou alias por truncamento | `as_int()` devolvia `None` para strings inválidas; campos opcionais eram interpretados como ausentes; valores inteiros eram truncados via `& 0xFFFFFFFF` | Na geração, rejeitar entradas **presentes** sem inteiro válido ou fora de `[-2^31, 2^32-1]`; manter valor omitido como opcional e valores negativos assinados 32-bit como compatíveis | `AppleAlcIdentityRound41Tests`: 12 entradas negativas (4 campos×3 valores), 10 anteriormente aceitas; 12 rejeitadas depois. Um teste positivo preserva `-1` e `-0x80000000` |
| Validador PE/COFF aceitava seção virtual ocupando RVAs dos cabeçalhos PE | Verificação anterior rejeitava sobreposição **raw** em arquivo, mas não mapeamento virtual com `SizeOfHeaders` | Em `validate_efi_image`, rejeitar `virtual_span != 0` com `virtual_address < size_of_headers` | `PeSectionHeadersRound41Tests.test_section_virtual_span_must_not_overlap_headers`: fixture malformada **aceita antes** e **rejeitada depois**, mantendo fixtures válidas aprovadas |

O risco comprovado da segunda falha é o validador deixar passar um binário PE/COFF malformado; não há demonstração de corrupção real de firmware. A análise do formato foi cotejada com a documentação PE da Microsoft (https://learn.microsoft.com/en-us/windows/win32/debug/pe-format), distinguindo esse material externo da execução dos testes no próprio source.

### Múltiplas inspeções independentes e evidências reprodutíveis

| Inspeção/execução | Escopo e prova | Resultado | O que **não** prova |
|---|---|---|---|
| Baseline integral em R40 | `python3 -B -m unittest discover -s Tests -p 'test*.py' -q` | 276/276 aprovados | driver carregado por UEFI |
| Regressão AppleALC pré-fix | Entradas inválidas em todos os quatro campos opcionais de identidade, execução da função real `generate` | 10/12 exposições indevidas; duas já rejeitadas | comportamento de codecs em hardware |
| Regressão PE pré-fix | Executor do Python embarcado no `.command` sobre PE sintético com seção virtual em RVA 0 | binário inválido aceito | exploração em firmware |
| Suite pós-fix completa | Regressões antigas e três testes novos | **279/279 aprovados** | cobertura integral de todas as funções |
| Inspeção de roteamento | Função original `HdaPathSearch`, extraída sem modificar corpo; comparação com BFS de referência em **20.000 grafos simulados**, instrumentada com ASan/UBSan | sem falhas detectadas; caminhos mínimos compatíveis | PCI real, transporte HDA, concorrência de eventos |
| Inspeção de áudio | Função original `HdaCopyBootChime`, extraída sem modificar corpo; **3.000 cenários** de PCM mono/estéreo, ganho fixo de 30%, sentinelas de buffer, ASan/UBSan | sem falhas detectadas | DMA real nem reprodução audível |
| Cinco rodadas reais do comando de build | `AUTO_FETCH_EDK2=0 SOURCE_TEST_ROUNDS=5 PYTHONDONTWRITEBYTECODE=1 ./Build-HDAUniversalBootChimeDxe.command` | **279/279 em cada uma** = **1.395** aprovações; cinco linhas `Ran 279 tests` e cinco `OK` no único log | build EFI: parado após o gate porque EDK2 ausente |
| Diferencial cumulativo | `.patch` único R37→R41 aplicado com `git apply --check`/`git apply` sobre R37 limpo; `cmp` de cinco arquivos | 5/5 identidades byte a byte; `git diff --check` sem erro | origem remota Git ou equivalência hardware |
| Integridade de sintaxe | `bash -n` scripts, AST Python e verificação de permissões | aprovados | ABI/compilação EDK2 |

Uma execução inicial do pipeline foi interrompida pelo tempo máximo de **uma chamada da ferramenta**, antes do fim da primeira rodada, sem emitir resultado conclusivo. O teste em que o log parou passou isoladamente; o pipeline repetido em processo acompanhado por consultas separadas completou as cinco rodadas. A primeira execução interrompida **não foi contada** como sucesso nem usada para alegar travamento do código.

### Inventário R41 de todos os 24 arquivos — integridade versus inspeção versus teste

A tabela distingue `I` = arquivo identificado/inventariado com bytes e modo; `M` = inspeção manual de implementações com escopo descrito (não implica automaticamente 100% da função); `T` = evidência de teste, host quando explícito; `H` = teste em hardware físico (**nenhum**). Os hashes são dos arquivos congelados antes da atualização deste adendo, exceto a linha do próprio `.md`, necessariamente autorreferente. O SHA-256 do ZIP de entrega é independente e será fornecido externamente.

| Arquivo | Linhas / tamanho | Modo | SHA-256 | Dependência/risco/status/evidência |
|---|---:|---:|---|---|
| `Audit/R38-AUDIT.md` | 1492 | `0644` | `autorreferente` | relatório único cumulativo; R38–R40 preservado, novo adendo R41; I/M (documental), sem autohash |
| `Audit/diffs/R37-to-R38.patch` | 510 | `0644` | `5d42b143e12c8480e65b1b3c2c7a3a4b3b9ba19f03a8b2169e27d390ec735912` | patch único R37→R41 (nome histórico); reaplicação limpa; I/M/T — git apply, cmp dos 5 arquivos |
| `Build-HDAUniversalBootChimeDxe.command` | 915 | `0755` | `b9931b49f575f26370b3a49efb7fbf4bee208ba938fae0280250479c17ce1d7b` | build, gate de fonte/PE, higiene e EDK2; I/M/T — análise manual do validador e regressão PE |
| `CHANGELOG.md` | 417 | `0644` | `2d26c0aa4ca11bf02c124eca3d06dc970d7e8a53a35db070b9fe7f855dbe7ad3` | histórico de versões intacto; I, herdado |
| `Enable-HDAUCodecDump-NextBoot.command` | 243 | `0755` | `c7f7cd4706733cd7696832b654a5c4431e700745036b3f747fa01e4ef336747a` | assistente macOS de dump; I/M histórico; sem execução macOS nesta rodada |
| `HDAUChimeDebug.ini` | 59 | `0644` | `fa7698619bf07ba1840ccc67c676ac3df2447ee318ddad6a85e4df36b76da246` | configuração DEBUG; I/M histórico; sem execução EFI |
| `HDAUChimeRelease.ini` | 59 | `0644` | `901561bc324f520002c82d5d02e3b29cc0f3ee44a0baf0283397fe3b66ee57e7` | configuração RELEASE; I/M histórico; sem execução EFI |
| `HDAUniversalBootChimeDxe/BootChimePcm.h` | 20293 | `0644` | `68220bf126b43627375ba27b6ef0cdbe7f8ceaa96b8ceb7c57b45c483e6a5c43` | PCM embutido de referência; I/T indireto; comparado por hash |
| `HDAUniversalBootChimeDxe/BootChimePcm44.h` | 18646 | `0644` | `1da445f511019536d2397373b5c4fb24dbe1ff38fe0ebdfafda039d030331d04` | PCM alternativa 44,1 kHz; I/T indireto; comparado por hash |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` | 10894 | `0644` | `2537228ca89640673f9b349bf170774b79ea3fd9f9c8a76ad4895b9e8966f96e` | driver HDA, DMA, callbacks, PCM; I/M dirigida/T 2 funções com ASan/UBSan; sem EFI |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf` | 45 | `0644` | `222f8f58ce0e519069bc2ee35b35848f141922cf725ffe334bebb1305012bff3` | definição do módulo EDK2; I/M histórico; sem EDK2 |
| `HDAUniversalBootChimeDxe/HdaAppleAlcLayouts.h` | 3672 | `0644` | `bd348bd45382a3b3bfa5902f2b56ca088b23a0418bf6d157a60046c72dd560a1` | tabelas AppleALC geradas; I/T consistência teste; sem hardware |
| `HDAUniversalBootChimeDxe/HdaUefi.h` | 577 | `0644` | `b6357b966e78babecfa497711851c099409a51192e0f38e11f917e1b5ec578f5` | tipos, limites e definições HDA; I/M dirigida; sem hardware |
| `HDAUniversalBootChimeDxePkg.dsc` | 33 | `0644` | `5ada46b1cb0eba180ce068d45d4ca54e96ba5ad4f7c39b180a8b5f236589a174` | descritor de plataforma EDK2; I/M histórico; sem EDK2 |
| `README.md` | 608 | `0644` | `f3d9a12ab86c078a7eb712a6e982f1977fb5e399ea83a30b60fffc643d388257` | documentação de utilização original; I, preservado |
| `Release/Logs/BUILD-COMPILE.log` | 1440 | `0644` | `9f7bedee47be6cbdff1a6996b2c6aeb38778f6f8357e121a08ab32e1f048ad65` | log único integrado 5×279 + bloqueio EDK2; I/M/T — contagens e saída do comando |
| `Setup-EDK2.command` | 85 | `0755` | `6a515d741b0ef47cff696a08d1b9aca4f5aae2ad558c9dfd41674994e56318c4` | preparação EDK2; I/M histórico; indisponibilidade real |
| `Tests/test_audit_regressions.py` | 4213 | `0755` | `c0ba74409127cffac2ac45ed28087c1dd6ca50846e5ee47bf95d9b82212ed001` | regressões Python, R38–R41; I/M novas três; T suíte 279 |
| `Tests/test_forcepin_runtime.py` | 171 | `0644` | `00d77fb92e43e0b97e50ad7eeb5406613421ffa3dc0aad1a22cfbef94cba16fb` | harness C ForcePin/fanout preservado; I/T suíte; M histórico |
| `Tools/generate_applealc_profiles.py` | 777 | `0755` | `1f5cef50b4b08a2b2608ae12e20d9bc317319ac43e30beee45cc5a65acaa26d5` | gerador AppleALC e validação de identidades; I/M/T regressão novos campos |
| `assets/BootChime.mp3` | 30407 bytes | `0644` | `7914bd202b1f539d1d8aafb7a371ee8008936bc5323094e52ca6439cf411e3ad` | MP3 original sem alteração; I — integridade de bytes |
| `docs/NO-SHELL-EFI-TEST.md` | 53 | `0644` | `892dbf1611ea71e258c234453a8942539b1a7efc8afda61f4c8eded5c1b2d5e8` | roteiro de ensaio EFI; I, preservado |
| `docs/PROJECT-REPORT.txt` | 370 | `0644` | `24c7735d7506a1dbf1c290165faf54eb949a484a25d54399439f01c2b6ea543c` | relatório original do produto, preservado; I, preservado |
| `docs/TESTING.md` | 81 | `0644` | `4adf5045d95beeb66d03fdd9749ff0d744ae3430293042f9b0dd1bf1e43a6b40` | procedimento histórico de testes; I, preservado |


### Matriz R41 por função C — inspeção dirigida e limites de cobertura

A matriz histórica de **212 funções C** neste relatório permanece válida para indexação, pois o arquivo `.c` não foi alterado. `M` abaixo significa que a função foi lida e examinada diretamente quanto aos riscos nomeados; `T-host` significa que a **implementação C real** foi exercitada em harness, e não apenas que um teste estático menciona seu nome. Não foi verificada a totalidade dos ramos dos 212 métodos nem suas interações de firmware.

| Função / trecho C | Faixa original aproximada | Inspeção efetiva | Limite ou dependência ainda aberta |
|---|---:|---|---|
| `HdaApplyConfigKey`, `HdaParseConfigBuffer`, `HdaReadConfigFromPath` | 631–841 | M: limites de parsing INI, ponteiros e leitura via EFI_FILE | API de arquivos UEFI não executada em hardware |
| `HdaOpenDebugLog`, `HdaReadAlcIdFromBootArgs` | 878–1224 | M: abertura de arquivo, parâmetros/retornos e buffers de boot-args | NVRAM/arquivos reais, TPL, Boot Services |
| `HdaCleanupAsyncPlayback` | 1575–1709 | M: cancelamento de timer, ownership, StopStatus, retorno com contexto retido, FreePool | callbacks concorrentes e firmware TPL não simulados |
| `HdaExitBootServicesCleanup` | 1711–1754 | M: política de contenção e renúncia de recursos após EBS | EBS real e retries do bootloader não reproduzidos |
| `HdaRequireControllerResetQuiescent` | 2053–2100 | M: condição de streams/rings em quiescência | registros HDA reais/firmware estranho |
| `HdaAbortImmediateCommand`, `HdaAcknowledgeImmediateResult`, `HdaImmediateCommandRaw` | 2226–2419 | M: transportes imediatos, busy, resposta e timeout | falhas intermitentes de hardware e canais CORB concorrentes |
| `HdaGetSubnodes` | 3932–3960 | M: faixas NID/capacidades e retornos | leituras reais dos codec subnodes |
| `HdaGetConnections` | 3960–4042 | M: expansão de ranges, Contagem `ConnMax`, overflow e transporte | testada em harness histórico, não integrada a controlador real R41 |
| `HdaPathSearch` | 4535–4621 | **M/T-host**: BFS, visited/parent, limites de 128 NIDs, rejeição de conexão inválida | 20.000 grafos simulados; funções HdaGetConnections/HdaGetParam stubbed |
| `HdaSelectPcmFormat` | 7196–7253 | M: PCM format, capacidades e caminho de falha | PLL/codecs DAC físicos não testados |
| `HdaCopyBootChime` | 8450–8479 | **M/T-host**: frames, ganho fixo, mono/estéreo, índices e clipping | 3.000 buffers simulados; não faz DMA |
| `HdaAllocatePlaybackBuffers` | 8481–8703 | M: overflow de tamanho, AllocatePages, Map, BDL, ownership e cleanup de erro | DMA/Map PCI real e faults de firmware não injetados R41 |
| `HdaFreePlaybackBuffers`, `HdaFinalizePlaybackContext` | 8705–8828 | M: Unmap/FreePages, ownership de tokens, falha terminal e retenção | FreePages real e comportamento de EFI_NOT_FOUND não ensaiados R41 |
| `HdaCaptureIdleStreamState`, `HdaRequireIdleStreamSnapshotUnchanged`, `HdaRequireStreamTagAvailable` | 8830–8982 | M: captura, revalidação de descritor e tag | concorrência física entre agentes PCI não testada |
| `HdaPrepareOutputStream` | 9218–9403 | M: formatação BDL, HDA register read-back e pré-requisitos de RUN | MMIO/stream HDA real não disponível |
| `HdaMonitorPlaybackProgress`, `HdaStartOutputStream` | 9406–9535 | M: LPIB, status, timeouts e checagem RUN | comportamento real de dispositivo e erros intermitentes |
| `HdaAsyncPlaybackTick`, `HdaStartFastBootPlayback` | 9815–10060 | M dirigida: caminho assíncrono, timer, FastBoot e rollback | não houve interleavings de callbacks TPL/UEFI reais |

**Cobertura honesta:** a identificação de 212 funções C e a leitura de várias regiões não são prova de revisão individual exaustiva; o restante das entradas da matriz histórica permanece como inventariado ou coberto apenas por asserções de regressão quando indicado. Casos de corrupção de memória, race conditions, deadlocks, vazamentos, perda de ownership e compatibilidade em hardwares reais continuam riscos residuais não descartados.

### Matriz R41 por função Python/validador — mudanças e regressões

| Função real | Faixa | Inspeção/teste e efeito colateral |
|---|---:|---|
| `as_int` (`Tools/generate_applealc_profiles.py`) | 104–119 | M: retorna `None` em valores inválidos; validação obrigatória quando campo está presente é tratada no chamador |
| `generate` (`Tools/generate_applealc_profiles.py`) | 237–720, correção ~440–460 | M/T: conversão de campos revision/subsystem, normalização 32-bit, geração de perfil com verbos. 12 negativos e 1 positivo; fluxos não atingidos continuam sem cobertura universal |
| `AppleAlcIdentityRound41Tests.test_malformed_and_out_of_range_identity_fields_do_not_emit_verbs` | 4165–4180 | M/T: 4×3 valores inválidos, rejeição de perfil e verbos |
| `AppleAlcIdentityRound41Tests.test_optional_signed_32bit_identity_values_still_work` | 4182–4189 | M/T: compatibilidade de inteiros negativos válidos |
| `validate_efi_image` (shell/Python embarcado) | 626–825, correção ~762–765 | M/T: section RVA sobre header mapeado, 1 novo teste de rejeição; checks preexistentes do R40 preservados |
| `PeSectionHeadersRound41Tests.test_section_virtual_span_must_not_overlap_headers` | 4204–4213 | M/T: fixture PE sintética, entrada válida no parser antigo porém seção invasora de RVA dos headers |
| `run_source_regressions`, `verify_source_tree_hygiene`, `cleanup_build_transaction` | 63–197 | M/T: execução real de cinco rodadas, metadados transitórios, limpeza e erro EDK2; não valida demais funções de EDK2 |

As outras funções Python e de shell continuam no inventário/matrizes anteriores e só podem ser chamadas de validadas quando houver evidências de cada caminho; o resultado **279/279** refere casos executados, não cobertura de linhas, branches ou 212 funções.

### Checkpoint persistente R41 e status real de Release

- **R40 entrada, ZIP:** `e898acc169edc184ba27d01c36f70660528ec25fb9967ee61a19c8abef177c11` (SHA-256 verificado neste host).
- **R41 driver C não modificado:** `2537228ca89640673f9b349bf170774b79ea3fd9f9c8a76ad4895b9e8966f96e`.
- **R41 gerador corrigido:** `1f5cef50b4b08a2b2608ae12e20d9bc317319ac43e30beee45cc5a65acaa26d5`.
- **R41 gate corrigido:** `b9931b49f575f26370b3a49efb7fbf4bee208ba938fae0280250479c17ce1d7b`.
- **R41 testes:** `c0ba74409127cffac2ac45ed28087c1dd6ca50846e5ee47bf95d9b82212ed001`.
- **R41 patch único cumulativo R37→R41:** `5d42b143e12c8480e65b1b3c2c7a3a4b3b9ba19f03a8b2169e27d390ec735912`, guardado no caminho histórico `Audit/diffs/R37-to-R38.patch` para satisfazer o gate de higiene; `git apply --check`, `git apply`, `cmp` de cinco arquivos e `git diff --check` aprovados.
- **R41 log único:** `9f7bedee47be6cbdff1a6996b2c6aeb38778f6f8357e121a08ab32e1f048ad65`, `Release/Logs/BUILD-COMPILE.log`; registra 5×279 aprovados e falha de preparo EDK2 (status não zero). Não reclassificar essa falha como compilação aprovada.
- **Ponto preciso de retomada para futura rodada:** preservar source R41 tal como empacotado; em host com EDK2 verificado, compilar DEBUG/RELEASE, conferir invariantes PE/COFF de ambos binários, testar em UEFI real os caminhos DMA/Map/Unmap/ExitBootServices/rollbacks/reentrância e codecs HDA; ampliar inspeção individual das funções ainda somente `I` na matriz, com evidências por linha e cenário. Não inventar ABI/firmware testado. Somente se houver defeitos reproduzidos, fazer a menor correção no arquivo de origem, atualizar esta mesma matriz/patch/log, reaplicar o patch e entregar novamente source completo.
- **Status:** fonte R41 corrigida e teste de host com evidência; **Release EFI NÃO CERTIFICADO**, pois não foi produzido `.efi`, compilado com EDK2 nem carregado em hardware. Auditoria funcional integral ainda aberta; não declarar 100%.


## ADENDO R42 — correção de permissões no gerador e ownership UEFI (19/10/2026)

### Proveniência, escopo e limites

- Base exclusiva da rodada: `HDAUniversalBootChimeDxe-v1.0.82-R41-CORRECTED-FULL-SOURCE.zip`, SHA-256 `ad3b319546e80e53234c0917ca93a22b60123216663aa1af9e53c3510ea3c42e`, confirmado localmente antes da extração. São 24 arquivos reais. Sem `.git` de origem no ZIP; histórico externo de commits e release remoto não comprovados.
- O produto permanece na versão `1.0.82` com todos os dados PCM/MP3, headers de perfis AppleALC, INI, INF, DSC, fontes e comandos existentes preservados, salvo os quatro arquivos necessários às duas correções e seus testes. Nenhuma nova funcionalidade ou configuração foi introduzida.
- Hashes e linhas medem **integridade e localização**, não inspeção efetiva nem cobertura funcional. A matriz R39 de 212 funções C continua como índice histórico; o arquivo C R42 recebeu somente +5 linhas em `HdaEnsureAppendLog`, de modo que as funções posteriores deslocaram cinco linhas. Nenhuma outra função C foi modificada nesta rodada. A análise de 100% das 212 funções NÃO está comprovada.

### Dois defeitos comprovados — origem, pré-correção, correção mínima e regressão

1. **Permissões perdidas pelo gerador AppleALC** (`Tools/generate_applealc_profiles.py`, `write_output_atomic`, linhas 723–759): `tempfile.NamedTemporaryFile` inicia arquivo temporário em modo `0600`; `os.replace` sobre arquivo gerado que já existia em modo `0644`, `0640` ou `0444` mudava silenciosamente o modo final para `0600`. Reproduzido com a função Python real e teste R42 executado **antes da correção** (três subcasos falharam). Corrigido preservando `stat.S_IMODE(output.stat().st_mode)` no arquivo temporário via `os.fchmod` antes da substituição; arquivo inexistente continua com o comportamento anterior. Falha simulada de `fchmod` é convertida em erro, mantém o conteúdo original e limpa o temporário. Testes `AppleAlcAtomicPermissionsRound42Tests` aprovados após correção.
2. **Log de diagnóstico órfão no erro de Close do root** (`HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c`, `HdaEnsureAppendLog`, linhas 4173–4201): quando `HdaOpenDebugLog` abria `mHdaLogFile` e o `HdaCloseFileHandleTerminal(&Root)` posterior falhava, a função retornava erro com `*OpenedLog = FALSE`, mas deixava `mHdaLogFile` aberto. O chamador `HdaCloseAppendLog` não podia encerrar esse handle. Reproduzido compilando **a função C de origem** com mocks EFI: executável retornava código de falha `12` pré-correção. Corrigido com `HdaCloseTrackedLogFile(FALSE)` no caminho do erro, mantendo o retorno original de `CloseStatus`. A mesma regressão passa após correção e testa também o caminho de sucesso que transfere a posse ao chamador. Não se presume que o mock substitui firmware UEFI real.

### Inspeção funcional adicional R42 por função (M = revisão dirigida, T-host = corpo executado com mocks/sanitizadores)

| Função de produção | Linhas R42 | Inspeção real / dependência e risco | Evidência / limite |
|---|---:|---|---|
| `HdaAsciiLower` | 226–229 | M: normalização ASCII sem escrita | T-host; não é parser Unicode completo |
| `HdaAsciiStartsWithInsensitive` | 1000–1009 | M: prefixos e terminação em NUL | T-host; entrada é buffer terminando em NUL |
| `HdaBootArgTokenBoundary` | 1011–1013 | M: delimitação de tokens de boot-args | T-host |
| `HdaFindAlcIdInAscii` | 1015–1063 | M: detecção `alcid=`, limites uint32, overflow, retorno; chamador `HdaReadAlcIdFromBootArgs` | T-host ASan/UBSan; NVRAM real pendente |
| `HdaBootArgIsEnabledToken` | 1067–1086 | M: valores `yes/on/true/1`, fronteira de tokens | T-host ASan/UBSan |
| `HdaFindCodecDumpRequestInAscii` | 1088–1109 | M: nomes aceitos e exclusão de substrings não delimitadas | T-host ASan/UBSan |
| `HdaConvertUtf16BootArgsInPlace` | 1111–1128 | M: leitura de pares UTF-16, avanço no mesmo buffer, NUL de saída | T-host ASan/UBSan; entrada NVRAM real não testada |
| `HdaDeferredCodecDumpNotify` | 1233–1327 | M: barreiras TPL, reentrância, cleanup, retry, evento de timer | Sem interleavings reais de firmware |
| `HdaServiceDeferredCodecDumpIfSafe` | 1329–1340 | M: chamadas sob seção crítica e pending | Sem timer firmware |
| `HdaUninstallBootScreenDNotify` | 1342–1404 | M: transação de unregistration, estados desconhecidos | Sem console UEFI físico |
| `HdaInstallBootScreenDNotify` | 1406–1571 | M: ownership de CreateEvent, RegisterKeyNotify e contenção | Sem callbacks hardware reais |
| `HdaInstallExitBootServicesCleanup` | 1757–1788 | M: validação do evento e status | Sem BeforeExitBootServices real |
| `HdaPollForF6` | 1790–1858 | M: orçamento finito de leitura e poll | Sem firmware teclado real |
| `HdaDebugInit` / `HdaDebugFinish` | 1860–1939 | M: inicialização e liberação de logging, arquivos e boot-args | Requer UEFI/FS real |
| `HdaOpenLoadedImageRoot` | 4123–4150 | M: transferência de handle e checagem de status | Sem OpenVolume real |
| `HdaDumpConnectionList` | 4155–4170 | M: limite de formatação e lista conexões | Hardware codec não testado |
| `HdaEnsureAppendLog` | 4173–4201 | **M/T-host**: nova captura e liberação do handle em falha de fechamento de root; retornos e ownership | Pré-fix exit `12`; pós-fix retorno `0` no harness |
| `HdaCloseAppendLog` | 4203–4208 | M: encerramento quando `OpenedLog` é verdadeiro | Stub, firmware File.Close pendente |

**C real executado isoladamente:** sete funções de parsing extraídas sem alterar seus corpos, com tipos UEFI simulados, testes dirigidos e **40.000 buffers determinísticos** sob Clang com ASan/UBSan, sem violações detectadas. Esses 40.000 casos são entradas simuladas, não execução de firmware, DMA ou áudio real. O `HdaEnsureAppendLog` foi compilado e executado separadamente com mocks de abertura, fechamento e cleanup para reproduzir o vazamento de ownership. Somam-se às inspeções antigas; não demonstram ausência geral de bugs, deadlocks ou memory leaks.

**Funções Python revisitadas:** `validate_plist_tree`, `parse_plist_bytes`, `load_plist_fragment`, `load_plist`, `as_int`, `leaf_node_chains`, `c_string`, `resource_member`, `read_plist_member`, `resource_root`, `pinconfigs_member`, `config_name`, `masked_constraints_overlap`, `exact_profile_is_subset`, `exact_profile_specificity`, `generate`, `write_output_atomic`, `main` (`Tools/generate_applealc_profiles.py`, linhas 43–781). Inspeção manual de implementações e fluxos; vários ramos de dados AppleALC, exceções e efeitos de I/O externos continuam sem prova de cobertura integral. As duas novas funções de teste de permissões estão em `Tests/test_audit_regressions.py` 4219–4245; o teste de ownership C está em `Tests/test_forcepin_runtime.py` 174–246.

### Rodadas e evidências pós-correção

| Verificação independente | Evidência/resultados | O que NÃO comprova |
|---|---|---|
| Testes das permissões e falha de `os.fchmod` | Dois testes R42 passaram, com modos 0644, 0640 e 0444 | Preservação de ACL/owner/xattrs em todos sistemas |
| Harness C de append-log | Pré-fix falha exit 12; pós-fix aprovação; erro de Close + sucesso normal | Fechamento de volumes UEFI físicos |
| Funcionalidade de boot-args C | 7 funções reais extraídas, 40.000 buffers + casos dirigidos, ASan/UBSan aprovados | GetVariable real, DMA, Boot Services ou callbacks TPL |
| Gate de fonte da versão R42 antes da correção C final | 281/281 aprovados; *versão intermediária*, não considerada Release final | O teste adicional de ownership só foi incluído depois |
| Pipeline integrado final `AUTO_FETCH_EDK2=0 SOURCE_TEST_ROUNDS=3 PYTHONDONTWRITEBYTECODE=1 ./Build-HDAUniversalBootChimeDxe.command` | **3 × 282/282 testes aprovados (846 aprovações)**; 3 rodadas independentes registradas no log único; status final não zero por EDK2 ausente | **NÃO compilou `.efi`**, DEBUG nem RELEASE; não testou hardware |
| Patch cumulativo R37→R42 | `git diff --check`, `git apply --check`, `git apply` sobre R37 limpo e `cmp` byte a byte dos 5 arquivos modificados/adicionados | Verificação de commits Git de origem externa |
| Diferença direta R41→R42 | Só 4 arquivos de código/testes alterados: `.c`, gerador e dois testes. Demais arquivos de produção intactos | Validação firmware da alteração C |
| Teste independente após extração do ZIP distribuível (prévia antes da atualização desta linha) | `unzip -t` íntegro; **24/24** hashes e modos Unix idênticos; `python3 -B -m unittest discover -s Tests -p "test*.py" -q` **282/282** aprovado na extração limpa | Sem EDK2 nem hardware; nenhum `.efi` |

A primeira tentativa de pipeline R42 com 281 testes foi interrompida intencionalmente quando a investigação identificou o segundo defeito. **Não foi contada como rodada completa** e não é substituto do pipeline com a versão final. Uma tentativa inicial de suíte R41 falhou somente porque uma importação manual de testes havia criado `__pycache__`; este diretório transitório foi removido sem enfraquecer o teste de higiene. A suíte R42 corrigida não contém esses artefatos.

### Inventário integral R42 — separar identificado, analisado, exercitado e testado em hardware

Legenda: **I** = integridade/identificação, **M** = inspeção humana localizada, **T** = teste citado; **H** = teste real de hardware (**nenhum**).

| Arquivo | Linhas/tamanho | Modo | SHA-256 | Evidência e dependências pendentes |
|---|---:|---|---|---|
| `Audit/R38-AUDIT.md` | 1713 | `0644` | `autorreferente — SHA-256 omitido` | I/M: documento cumulativo + matriz; conteúdo não é cobertura funcional |
| `Audit/diffs/R37-to-R38.patch` | 660 | `0644` | `ba761ebdfaf35f0e78de47c1dfc2c5ea15e3965f4e7f81cd7451a6eac79aee05` | I/M/T: patch cumulativo R37→R42, reaplicado e comparado |
| `Build-HDAUniversalBootChimeDxe.command` | 915 | `0755` | `b9931b49f575f26370b3a49efb7fbf4bee208ba938fae0280250479c17ce1d7b` | I/M dirigida: PE/COFF e gate; T: 3 rodadas (sem EDK2) |
| `CHANGELOG.md` | 417 | `0644` | `2d26c0aa4ca11bf02c124eca3d06dc970d7e8a53a35db070b9fe7f855dbe7ad3` | I: documento histórico preservado; revisão de redação fora do escopo |
| `Enable-HDAUCodecDump-NextBoot.command` | 243 | `0755` | `c7f7cd4706733cd7696832b654a5c4431e700745036b3f747fa01e4ef336747a` | I; sem inspeção integral ou teste específico R42 |
| `HDAUChimeDebug.ini` | 59 | `0644` | `fa7698619bf07ba1840ccc67c676ac3df2447ee318ddad6a85e4df36b76da246` | I/M: configuração e metadata conferida, sem teste EFI |
| `HDAUChimeRelease.ini` | 59 | `0644` | `901561bc324f520002c82d5d02e3b29cc0f3ee44a0baf0283397fe3b66ee57e7` | I/M: configuração e metadata conferida, sem teste EFI |
| `HDAUniversalBootChimeDxe/BootChimePcm.h` | 20293 | `0644` | `68220bf126b43627375ba27b6ef0cdbe7f8ceaa96b8ceb7c57b45c483e6a5c43` | I: byte a byte preservado vs R41; sem execução hardware |
| `HDAUniversalBootChimeDxe/BootChimePcm44.h` | 18646 | `0644` | `1da445f511019536d2397373b5c4fb24dbe1ff38fe0ebdfafda039d030331d04` | I: byte a byte preservado vs R41; sem execução hardware |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` | 10899 | `0644` | `2c32c4039cd65a595ccda053a35b67da23a0a3ce66823ac6e141f153b4d9adaf` | I; M dirigida: boot-args, callbacks, log, ciclo de vida; T-host: parsing e log; sem firmware |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf` | 45 | `0644` | `222f8f58ce0e519069bc2ee35b35848f141922cf725ffe334bebb1305012bff3` | I/M: configuração e metadata conferida, sem teste EFI |
| `HDAUniversalBootChimeDxe/HdaAppleAlcLayouts.h` | 3672 | `0644` | `bd348bd45382a3b3bfa5902f2b56ca088b23a0418bf6d157a60046c72dd560a1` | I: byte a byte preservado vs R41; sem execução hardware |
| `HDAUniversalBootChimeDxe/HdaUefi.h` | 577 | `0644` | `b6357b966e78babecfa497711851c099409a51192e0f38e11f917e1b5ec578f5` | I: tipos/constantes; revisão de dependências parcial |
| `HDAUniversalBootChimeDxePkg.dsc` | 33 | `0644` | `5ada46b1cb0eba180ce068d45d4ca54e96ba5ad4f7c39b180a8b5f236589a174` | I/M: configuração e metadata conferida, sem teste EFI |
| `README.md` | 608 | `0644` | `f3d9a12ab86c078a7eb712a6e982f1977fb5e399ea83a30b60fffc643d388257` | I: documento histórico preservado; revisão de redação fora do escopo |
| `Release/Logs/BUILD-COMPILE.log` | 879 | `0644` | `b12ce13bb9532c460a78bfd9aa8f7dad933910f8155db62e5940736fb1427c0f` | I/M/T: três rodadas completas + bloqueio EDK2 |
| `Setup-EDK2.command` | 85 | `0755` | `6a515d741b0ef47cff696a08d1b9aca4f5aae2ad558c9dfd41674994e56318c4` | I/M: semântica de checkout/comando e pré-requisitos; EDK2 ausente |
| `Tests/test_audit_regressions.py` | 4242 | `0755` | `5798ccd1dd01d2f2a971fe131e7bbb6ef96752f2c04a184083d29af08f4b14c9` | I/M: duas regressões R42; T: suite 282 |
| `Tests/test_forcepin_runtime.py` | 248 | `0644` | `a16feebe5d9fe1f0826816fd48da559cc2e56c0956df039aaff696017a1f26be` | I/M: teste C real de ownership e regressões ForcePin; T-host |
| `Tools/generate_applealc_profiles.py` | 786 | `0755` | `bd834396b1205e4bddb146c1a6c7221ce096971cd630a2b0c013918297fb4a47` | I/M: leitura das funções e fluxo de escrita atômica; T: modos e falha fchmod |
| `assets/BootChime.mp3` | 30407 bytes | `0644` | `7914bd202b1f539d1d8aafb7a371ee8008936bc5323094e52ca6439cf411e3ad` | I: byte a byte preservado vs R41; sem execução hardware |
| `docs/NO-SHELL-EFI-TEST.md` | 53 | `0644` | `892dbf1611ea71e258c234453a8942539b1a7efc8afda61f4c8eded5c1b2d5e8` | I: documento histórico preservado; revisão de redação fora do escopo |
| `docs/PROJECT-REPORT.txt` | 370 | `0644` | `24c7735d7506a1dbf1c290165faf54eb949a484a25d54399439f01c2b6ea543c` | I: documento histórico preservado; revisão de redação fora do escopo |
| `docs/TESTING.md` | 81 | `0644` | `4adf5045d95beeb66d03fdd9749ff0d744ae3430293042f9b0dd1bf1e43a6b40` | I: documento histórico preservado; revisão de redação fora do escopo |

### Checkpoint persistente e status real de Release R42

- Source de entrada R41: ZIP SHA-256 `ad3b319546e80e53234c0917ca93a22b60123216663aa1af9e53c3510ea3c42e`, confirmado.
- Source R42: 24 arquivos distribuíveis; o próprio relatório é autorreferente e não inclui sua hash por arquivo na tabela. Integridade final do ZIP reportada fora dele após empacotamento.
- Driver C R42: SHA-256 `2c32c4039cd65a595ccda053a35b67da23a0a3ce66823ac6e141f153b4d9adaf`; gerador R42: `bd834396b1205e4bddb146c1a6c7221ce096971cd630a2b0c013918297fb4a47`.
- Patch consolidado no único `Audit/diffs/R37-to-R38.patch` (nome histórico preservado por compatibilidade com os testes): SHA-256 `ba761ebdfaf35f0e78de47c1dfc2c5ea15e3965f4e7f81cd7451a6eac79aee05`, aplicável a R37 limpo.
- Um único relatório de auditoria (`Audit/R38-AUDIT.md`), um único patch e um único log (`Release/Logs/BUILD-COMPILE.log`); documentação histórica README/CHANGELOG/docs mantida intocada para não alterar escopo.
- **Próximo ponto exato:** compilar DEBUG/RELEASE no EDK2 `edk2-stable202608`/commit configurado, validar PE/COFF dos binários reais, testar em máquina com controlador Intel HDA os fluxos de File.Close falho, timer, DMA, CORB/RIRB, snapshot/rollback, reentrância e handoff `ExitBootServices`; em paralelo, continuar inspeção função por função das 212 definições C indexadas sem status M/T suficiente. Novas correções somente após reprodução antes/depois.
- **Status de Release: NÃO CERTIFICADO.** A fonte R42 está corrigida e validada nos testes de host listados; sem compilação EDK2, assinatura firmware nem boot HDA real. **Cobertura funcional integral: não comprovada.**


## ADENDO R43 — dump de conexões HDA (19/10/2026)

### Proveniência, integridade e escopo

- **Última base entregue, exclusiva desta rodada:** `HDAUniversalBootChimeDxe-v1.0.82-R42-CORRECTED-FULL-SOURCE.zip`, SHA-256 `a9999e5c0ff7d7cb5a3bcdb26aef184ad684801e83e1c29bbace9046c96f5736`, conferido antes de extrair e alterar. São 24 arquivos, sem diretório `.git` ou commits de origem verificáveis no ZIP. Não foi inferido um commit remoto.
- Comparação direta R42→R43: modificados somente `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` e `Tests/test_forcepin_runtime.py` no código/testes, mais a atualização dos três registros existentes (`Audit/R38-AUDIT.md`, `Audit/diffs/R37-to-R38.patch`, `Release/Logs/BUILD-COMPILE.log`). Sem alteração em dados PCM, MP3, cabeçalhos gerados, INI, INF, DSC, ferramentas ou outros scripts.
- A versão do produto permanece **1.0.82**. O nome histórico do arquivo patch foi preservado para a compatibilidade do teste de higiene; o seu conteúdo é cumulativo **R37→R43**.
- Leitura de arquivo, inspeção dirigida de implementação, teste sob host e validação em UEFI real são evidências distintas. A análise de 100% das funções C não está demonstrada.

### Defeito comprovado R43-C01 — perda silenciosa no dump de lista de conexões

**Origem:** `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c`, função real `HdaDumpConnectionList`, linhas aproximadamente 4155–4178 após a correção. A enumeração real `HdaGetConnections` aceita até a capacidade configurada, mas o código antigo fazia `for (... && Off + 10 < sizeof(Line))`, interrompendo a emissão dos nós sem sinalizar erro. Mesmo com buffer expandido, `HdaLog` possui buffer de **512 bytes** e prefixa `[HDAUDUMP] `; uma linha grande seria truncada pelo logger.

**Reprodução antes:** novo teste C compila o corpo original de `HdaDumpConnectionList` extraído do R42, simula a coleta de NIDs e a formatação de `HdaLog` com seu limite de 512 bytes. Para lista de 100 conexões, o teste falhava com **`missing connection 97 of 100 (lines=1)`**. O test harness não é um mock do algoritmo; contém a função original e stubs de I/O, impressão e coleta. Não existe afirmação de reprodução em hardware.

**Correção:** manter `Line[512]`, a ordem de visita e a linha única para listas pequenas; emitir fragmentos adicionais quando a linha se aproxima do limite, reservando folga para o prefixo real do logger. Cada NID continua na mesma ordem, sem mudar seleção de dispositivos, criação de rotas ou programação do codec.

**Validação depois:** os casos 1, 50, 100 e 127 conexões passaram; execução independente com **11 limites (1, 50, 90, 95, 96, 97, 98, 99, 100, 110 e 127)** sob Clang ASan/UBSan terminou sem relatório de violação. A saída foi verificada para conter todas as conexões; a representação final por várias linhas é deliberada para entradas grandes.

**Diff R42→R43:** 6 linhas líquidas acrescentadas no `.c`; somente novo teste de regressão em `Tests/test_forcepin_runtime.py`. Não atribuir resultados novos a arquivos não modificados.

### Matriz complementar R43 — funções e dependências efetivamente revisadas

Legenda: `I` = identificada / lida ou hash; `M` = trecho examinado diretamente e investigado; `T` = execução de host; `H` = execução firmware/hardware HDA (**nenhuma**).

| Função (arquivo C, exceto indicado) | Região de interesse | Cobertura comprovável / riscos ainda abertos |
|---|---:|---|
| `HdaDumpConnectionList` | 4155–4178 | **M/T:** antes e depois com 4 comprimentos; 11 comprimentos sanitizados. Formatação e continuação; **H pendente** |
| `HdaGetConnections` | 3959–4038 | **M parcial:** contagem, expansão de ranges, limitação `ConnMax`; mocks do leitor no teste novo. A função de transporte real **não** foi exercitada nesta rodada |
| `HdaDumpWrite` | 4045–4056 | **M parcial:** encaminhamento formatado ao `HdaLog`; teste simula o efeito de tamanho; sem sistema de arquivos EFI |
| `HdaLog` | 556–604 | **M parcial:** buffer 512, cabeçalho, sink e flush; a simulação reproduziu truncamento de linha; callbacks reais de arquivo ainda não testados |
| `HdaGetSubnodes` | 3932–3957 | **M dirigida:** limites de ID 7 bits e intervalo. Sem descoberta em codec real |
| `HdaReadConfigFromPath`, `HdaOpenDebugLog`, `HdaEnsureAppendLog`, `HdaCloseAppendLog` | 781–995, 4179–4210 | **M parcial:** abertura, leitura, close terminal e ownership; regressão histórica R42 permanece, sem novo teste UEFI |
| `HdaSelectPcmFormat`, `HdaBuildAlcIdDacOrder` | 7207–7327 | **M parcial:** fallback PCM e capacidade de ordem de DACs; nenhuma mudança |
| `HdaAllocatePlaybackBuffers`, `HdaFreePlaybackBuffers`, `HdaMonitorPlaybackProgress`, `HdaStartOutputStream` | 8492–8809, 9417–9549 | **M dirigida:** mapping/unmapping, ownership, stop/drain, descritor; ainda sem controlador físico ou compilação EDK2 |
| `HdaGetPciAttributesExact`, `HdaApplyPciAttributeBits`, `HdaRestorePciAttributes` | 9549–9668 | **M parcial:** restauração de atributos PCI; sem simulação independente R43 |
| `HdaDumpAllControllers`, `HdaDumpAllControllersDynamic`, `HdaUniversalBootChimeDxeEntry` | 10351–10905 | **M parcial:** ciclo do dump, log global, ponto de entrada e critical section; sem boot EFI |
| `ConnectionDumpRound43Tests.test_all_connection_nodes_are_logged_without_truncation` (`Tests/test_forcepin_runtime.py`) | 250–345 | **M/T:** compilação do C real, limite do logger em stub, verificação dos tokens |
| `write_output_atomic`, `generate` (`Tools/generate_applealc_profiles.py`) | 238–786 | **I/M histórica R39–R42:** sem alteração ou novo problema comprovado R43 |

As **212 definições C** da matriz histórica permanecem inventariadas. Esta matriz é **complementar**, não declara que todas as definições foram analisadas com a mesma profundidade ou que estão sem defeitos.

### Testes, integração e limitações

| Verificação independente | Evidência |
|---|---|
| Suíte do ZIP R42 antes da correção R43 | **282/282** aprovados neste host (125,714 s) |
| Regressão nova executada no C original pré-correção | **Falha reproduzida**, conexão 97 ausente em lista de 100 |
| Regressão nova executada após alterar `.c` | **Passou**, 4 comprimentos incluindo 127 |
| Host Clang, ASan/UBSan | **11 casos**, saída íntegra, sem violações detectadas |
| Sintaxe shell/Python | `bash -n` dos três comandos e AST dos três módulos Python aprovados |
| Patch único cumulativo R37→R43 | `git diff --check`, `git apply --check`, aplicação limpa e `cmp` de **5 arquivos** aprovados |
| Pipeline real `AUTO_FETCH_EDK2=0 SOURCE_TEST_ROUNDS=3` | **3 rodadas completas, 3 suites aprovadas, 283 testes por rodada**; término = `1` |
| Compilação EFI DEBUG/RELEASE | **Não validada**, falta workspace EDK2 no host de execução |
| Dispositivo HDA físico e handoff `ExitBootServices` | **Não realizados**, não afirmar certificação de Release |

### Inventário do pacote R43 (todos os arquivos reais)

A coluna abaixo é integridade de bytes, **não** cobertura funcional. O SHA-256 do relatório é autorreferente e por isso não consta de sua própria tabela. A árvore deve continuar com **24 arquivos**, um único `.patch`, um único relatório de auditoria e um único log de build. README, CHANGELOG e documentação histórica são preservados intocados conforme regra de alterações mínimas.

| Caminho | Linhas / bytes | Modo Unix | SHA-256 |
|---|---:|---:|---|
| `Audit/R38-AUDIT.md` | 1713 linhas | `0644` | autorreferente |
| `Audit/diffs/R37-to-R38.patch` | 774 linhas | `0644` | `e1b9eb15d9aeede2df13e60dc1275483ba55d49384cfef45b4448b70a9bc0fb7` |
| `Build-HDAUniversalBootChimeDxe.command` | 915 linhas | `0755` | `b9931b49f575f26370b3a49efb7fbf4bee208ba938fae0280250479c17ce1d7b` |
| `CHANGELOG.md` | 417 linhas | `0644` | `2d26c0aa4ca11bf02c124eca3d06dc970d7e8a53a35db070b9fe7f855dbe7ad3` |
| `Enable-HDAUCodecDump-NextBoot.command` | 243 linhas | `0755` | `c7f7cd4706733cd7696832b654a5c4431e700745036b3f747fa01e4ef336747a` |
| `HDAUChimeDebug.ini` | 59 linhas | `0644` | `fa7698619bf07ba1840ccc67c676ac3df2447ee318ddad6a85e4df36b76da246` |
| `HDAUChimeRelease.ini` | 59 linhas | `0644` | `901561bc324f520002c82d5d02e3b29cc0f3ee44a0baf0283397fe3b66ee57e7` |
| `HDAUniversalBootChimeDxe/BootChimePcm.h` | 20293 linhas | `0644` | `68220bf126b43627375ba27b6ef0cdbe7f8ceaa96b8ceb7c57b45c483e6a5c43` |
| `HDAUniversalBootChimeDxe/BootChimePcm44.h` | 18646 linhas | `0644` | `1da445f511019536d2397373b5c4fb24dbe1ff38fe0ebdfafda039d030331d04` |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c` | 10905 linhas | `0644` | `9cd01327dfaca30d88b903aeea145e63dd16b9238a2c73d83280bef134fead45` |
| `HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf` | 45 linhas | `0644` | `222f8f58ce0e519069bc2ee35b35848f141922cf725ffe334bebb1305012bff3` |
| `HDAUniversalBootChimeDxe/HdaAppleAlcLayouts.h` | 3672 linhas | `0644` | `bd348bd45382a3b3bfa5902f2b56ca088b23a0418bf6d157a60046c72dd560a1` |
| `HDAUniversalBootChimeDxe/HdaUefi.h` | 577 linhas | `0644` | `b6357b966e78babecfa497711851c099409a51192e0f38e11f917e1b5ec578f5` |
| `HDAUniversalBootChimeDxePkg.dsc` | 33 linhas | `0644` | `5ada46b1cb0eba180ce068d45d4ca54e96ba5ad4f7c39b180a8b5f236589a174` |
| `README.md` | 608 linhas | `0644` | `f3d9a12ab86c078a7eb712a6e982f1977fb5e399ea83a30b60fffc643d388257` |
| `Release/Logs/BUILD-COMPILE.log` | 882 linhas | `0644` | `3bbec4c6378416f36279c0b818e136d328177172634d4f6eba0c0066fd9343dc` |
| `Setup-EDK2.command` | 85 linhas | `0755` | `6a515d741b0ef47cff696a08d1b9aca4f5aae2ad558c9dfd41674994e56318c4` |
| `Tests/test_audit_regressions.py` | 4242 linhas | `0755` | `5798ccd1dd01d2f2a971fe131e7bbb6ef96752f2c04a184083d29af08f4b14c9` |
| `Tests/test_forcepin_runtime.py` | 347 linhas | `0644` | `80dddc442e747c8867b1146a055782300beaa633b5864890e2210bf4ad4d5286` |
| `Tools/generate_applealc_profiles.py` | 786 linhas | `0755` | `bd834396b1205e4bddb146c1a6c7221ce096971cd630a2b0c013918297fb4a47` |
| `assets/BootChime.mp3` | 30407 bytes | `0644` | `7914bd202b1f539d1d8aafb7a371ee8008936bc5323094e52ca6439cf411e3ad` |
| `docs/NO-SHELL-EFI-TEST.md` | 53 linhas | `0644` | `892dbf1611ea71e258c234453a8942539b1a7efc8afda61f4c8eded5c1b2d5e8` |
| `docs/PROJECT-REPORT.txt` | 370 linhas | `0644` | `24c7735d7506a1dbf1c290165faf54eb949a484a25d54399439f01c2b6ea543c` |
| `docs/TESTING.md` | 81 linhas | `0644` | `4adf5045d95beeb66d03fdd9749ff0d744ae3430293042f9b0dd1bf1e43a6b40` |

### Checkpoint persistente e estado final R43

- **ZIP de entrada R42:** `a9999e5c0ff7d7cb5a3bcdb26aef184ad684801e83e1c29bbace9046c96f5736`.
- **Driver C R43:** `9cd01327dfaca30d88b903aeea145e63dd16b9238a2c73d83280bef134fead45`; **teste C R43:** `80dddc442e747c8867b1146a055782300beaa633b5864890e2210bf4ad4d5286`; **patch cumulativo R37→R43:** `e1b9eb15d9aeede2df13e60dc1275483ba55d49384cfef45b4448b70a9bc0fb7`; **log integrado:** `3bbec4c6378416f36279c0b818e136d328177172634d4f6eba0c0066fd9343dc`.
- O patch continua no único caminho `Audit/diffs/R37-to-R38.patch` por compatibilidade com o gate. O documento cumulativo continua em `Audit/R38-AUDIT.md`, sem multiplicar relatórios.
- **Próximo ponto exato:** partir do ZIP R43 íntegro; repetir compilação EDK2 `edk2-stable202608`/commit esperado (`2970e5699ba6267f3384ffab20f96647578aebc8`) para DEBUG e RELEASE com workspace verificável; carregar o DXE em ambiente UEFI/HDA físico; registrar fluxo das conexões longas, CORB/RIRB, Map/Unmap, restore, callbacks e `ExitBootServices`; inspecionar individualmente os demais ramos de todas as funções ainda sem status M/T suficiente. Somente corrigir novos defeitos com reprodução antes/depois e atualização deste mesmo arquivo e patch.
- **Release:** NÃO CERTIFICADO. **Auditoria funcional 100%:** NÃO COMPROVADA. Os testes de host não são substitutos para firmware ou hardware.
