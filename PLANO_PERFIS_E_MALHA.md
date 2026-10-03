# Plano: perfis de comunicação e malha de controle

Criado: 2026-10-01
Repositórios: BTP, BTPDevice, TraceView (módulo TraceLab), bally_OS
Estado: **aprovado em 2026-10-01**, em implementação. Decisões da revisão na §9.

---

## 1. Objetivo

Definir uma "língua" única para que o robô simulado, o TraceView/TraceLab e o
robô real troquem dados e controle **do mesmo jeito**, em todos os estágios do
desenvolvimento:

| Estágio | Planta | Controle | O que muda ao passar para o próximo |
|---|---|---|---|
| **1. Simulação completa** | robô simulado (TraceLab) | TraceLab | — |
| **2. Robô real, controle no PC** | robô real | TraceLab | só o dispositivo escolhido |
| **3. Tudo embarcado** | robô real | firmware | o controlador é compilado no firmware; o TraceView só observa |
| HIL (depois) | robô simulado | firmware | plano próprio, usa a mesma língua |

O critério de sucesso deste plano: **passar do estágio 1 para o 2 sem mudar
dashboard, controlador nem configuração**, só trocando o dispositivo. Passar do 2
para o 3 é recompilar o mesmo controlador dentro do firmware.

### Fora deste plano

- O simulador em si (modelo, pista, widget Pista) e o HIL: planos próprios.
- Migração do bally_OS para a BTPDevice: já está na Fase 6 da BTPDevice.

---

## 2. O que existe hoje (levantamento)

**BTP (v2.49.0)**
- Cada campo do manifest traz: nome, unidade, descrição, min/max (`HAS_RANGE`),
  escala/offset, enum e array. Cada tópico traz: nome, descrição e taxa máxima.
  (`docs/commands.md` §3.6–3.7)
- Ações (comandos) são descritas no manifest com **parâmetros tipados no mesmo
  formato de campo** e flags `IDEMPOTENT` (bit 0) e `DANGEROUS` (bit 1). Os demais
  bits estão livres. (§3.9)
- Telemetria não tem garantia de entrega e não é retransmitida. O `sequence` é
  compartilhado por todos os tipos de mensagem do produtor, então um buraco nele
  não prova perda de telemetria. (`docs/model.md` §4.3, §7.1)
- Comando é pergunta/resposta, com retry, deduplicação por
  (source, boot, sequence) e prioridade alta. O cache de deduplicação é limitado
  (`BUSY / CAPACITY_EXHAUSTED`). (`docs/commands.md` §2.5–2.6)
- A especificação diz que cor, tipo de gráfico e eixos não são metadados de
  telemetria e ficam no cliente. (`docs/telemetry.md` §18.3)

**BTPDevice**
- Os tópicos são declarados por builder, com nome, unidade e faixa
  (`include/btpdev/topics.h`), e só são publicados enquanto há assinatura. Também
  existem tópico de texto e stream de blocos (com `seq`).
- Os comandos são linhas do TinyShell, transportadas por **duas ações fixas**: 1 = linha de
  shell e 2 = ping (`src/protocol/ShellText.h`). **Não existem ações tipadas no
  manifest.** "Parâmetros persistentes declarados como os tópicos" aparece em
  "Depois da v1" (`docs/PLANO.md`).
- A task `btpdev_comms` é a única dona do `btp::Node`, e os comandos rodam na
  `btpdev_shell`.

**TraceView**
- Os bindings dos widgets são salvos por **id numérico** (`sourceId`, `topicId`,
  `fieldId`). O `sourceId` vem do MAC, então **um dashboard feito para o robô
  simulado não funciona no robô real**, e um firmware que reordena tópicos quebra
  os dashboards.
- O editor de gráfico já usa a unidade e o min/max de cada série e tem "eixo
  automático por unidade". O trabalho manual está em adicionar as séries uma a uma.
- Os widgets de controle mandam texto cru, e o `docs/PROTOCOL.md` já registra que
  isso pode corromper o fluxo do protocolo.

**bally_OS**
- Usa uma pilha BTP própria (`robot.state`, `robot.sensors`, `robot.flags`,
  `system.monitor`, `protocol.test`), tem EKF, IMU, encoders e array de sensores.
  **Ainda não tem a malha de controle.**

---

## 3. Decisões

| # | Decisão | Motivo |
|---|---|---|
| D1 | **Um contrato só para todos os estágios.** Muda apenas onde cada peça roda | É o que torna fácil passar dados e controle de um lado para o outro |
| D2 | **Configuração tem que chegar (ação confiável). Amostra pode se perder (telemetria/stream)** | Retransmitir uma amostra velha é pior que perdê-la. Modo, ganhos e "armar" precisam de confirmação |
| D3 | **Bind por nome:** (dispositivo, tópico, campo, elemento), nunca por id | Permite que o mesmo dashboard e o mesmo controlador funcionem no simulado e no real |
| D4 | **Perfis são convenções de nomes sobre o BTP.** Evoluem só de forma aditiva (campos novos são ignorados por quem não conhece). Uma quebra exige um nome novo | Assim não é preciso versionar perfis no fio |
| D5 | **O dashboard enviado pelo dispositivo é um documento opaco (template)**, não metadado de campo | Respeita a regra do BTP §18.3: a apresentação continua sendo do cliente |
| D6 | **A segurança fica sempre no robô:** stream só move o motor se a malha foi armada por ação confiável. Watchdog obrigatório | O link pode cair a qualquer momento |
| D7 | **Uma única mudança no fio: a flag `STREAM` nas ações** | Comando não serve a 80 Hz. As alternativas sem mudar o BTP são piores (ver §4.6) |
| D8 | **Controlador portátil:** uma API C mínima, e o mesmo arquivo compila no TraceLab e no ESP32 | É o que viabiliza o estágio 3 |
| D9 | **O robô simulado é um dispositivo BTP de verdade** (transporte em loopback dentro do processo), sem atalho por fora do protocolo | O estágio 1 já exercita exatamente o caminho do estágio 2 |
| D10 | **A medição é ordenada pelo perfil.** O controlador recebe vetores (`y[]`, `u[]`) na ordem que o perfil define, e o host traduz nomes para posições | O controlador não depende do manifest e roda igual no firmware |

---

## 4. A língua (rascunho da especificação da F1)

### 4.1 Regras gerais

- **Prefixos reservados:** `loop.` (malha genérica), `lf.` (seguidor de linha) e
  `tv.` (meta do TraceView). Os tópicos do próprio produto usam outros prefixos.
- **Bind por nome:** o par (tópico, campo) é a identidade. Ids são detalhe de transporte.
- **Campo obrigatório ausente:** o cliente avisa e não liga a funcionalidade.
  **Campo desconhecido:** é ignorado.
- **Unidade e faixa vêm do manifest.** O cliente usa esses dados para eixos,
  sliders e saturação do controlador.

### 4.2 Perfil `loop` (qualquer malha fechada)

| Nome | Tipo | Sentido | Campos | Entrega |
|---|---|---|---|---|
| `loop.state` | tópico | robô → | `tick` u32, `mode` u8 (enum) + **medições nomeadas** (com unidade/faixa) | telemetria, na taxa da malha |
| `loop.output` | tópico | robô → | `ref_tick` u32, `source` u8 (enum: onboard/remoto/mantido/reserva/parado) + **entradas aplicadas** | telemetria, na taxa da malha |
| `loop.stats` | tópico | robô → | `rx`, `lost`, `late`, `held`, `fallback` (u32), `age_max` u16 (ticks), `period_us` u32 | telemetria, ~2 Hz |
| `loop.u` | ação **STREAM** | → robô | `ref_tick` u32 + **cada entrada como f32[≤M]** (horizonte, `[0]` = agora) | stream |
| `loop.inject` | ação **STREAM** | → robô | `tick` u32 + medições nomeadas (só no HIL) | stream |
| `loop.mode` | ação confiável, `DANGEROUS` | → robô | `mode` (OFF/ONBOARD/REMOTE/HIL), `arm` bool | comando |
| `loop.config` | ação confiável | → robô | `hold_ticks`, `timeout_ticks`, `fallback` (enum) + parâmetros do controlador | comando |

**Correlação:** `tick` é um contador do robô, em campo próprio, porque o
`sequence` do BTP é compartilhado. Toda atuação devolve o `ref_tick` da amostra
que a originou. O robô calcula `idade = tick_atual − ref_tick` com o próprio
relógio, sem precisar de sincronização, e essa mesma conta dá a latência da malha.

**Política no robô, a cada tick (modo REMOTE):**

```
se chegou loop.u com idade ≤ hold_ticks   → aplica u[idade]        (source = remoto)
senão, se o último plano ainda cobre o tick → aplica o próximo passo do plano (source = mantido)
senão, se passaram < timeout_ticks          → aplica a política de reserva:
                                              manter / controle embarcado / zerar (source = reserva)
senão                                       → motores desligados, desarma (source = parado)
publica loop.output e atualiza loop.stats
```

**Sem horizonte (PD):** M = 1. **Com MPC:** manda o plano inteiro, e um pacote perdido é
coberto pelo plano anterior (*packetized predictive control*).

**No PC:** o controle roda **quando chega o `loop.state`**, sem timer próprio.
Ele fica sincronizado ao relógio do robô, e um MPC pode compensar o atraso medido.

### 4.3 Perfil `lf` (seguidor de linha)

Define **quais** medições e entradas o `loop.*` carrega, e em que ordem (D10):

| Vetor | Ordem | Campos |
|---|---|---|
| `y` (em `loop.state`) | 0–14 | `s` f32[15], 0–1 (sensores normalizados) |
| | 15–16 | `w_l`, `w_r` f32, rad/s |
| | 17 | `gyro_z` f32, rad/s (opcional, NaN se ausente) |
| `u` (em `loop.u` / `loop.output`) | 0–1 | `u_l`, `u_r` f32, %, faixa −100..100 |

Tópicos de observação, que não entram na malha:

- `lf.pose`: `x`, `y` (m), `theta` (rad), estimativa do EKF.
- `lf.truth`: **só o simulado publica**. Pose real, erro lateral e progresso na
  pista, para comparar a estimativa com a verdade.

### 4.4 `tv.dashboard`: o dispositivo manda o próprio dashboard

- É um tópico com codificação `JSON_UTF8`, publicado uma vez quando alguém assina
  (`OnEvent`).
- Formato `tvdash/1`: widgets amarrados por nome, posição na grade e estilo opcional.

```json
{
  "format": "tvdash/1", "title": "Seguidor", "revision": 3,
  "widgets": [
    { "type": "line", "title": "Rodas", "at": [0, 0, 6, 3],
      "series": [ { "topic": "loop.state", "field": "w_l" },
                  { "topic": "loop.state", "field": "w_r" } ] },
    { "type": "bar", "title": "Sensores", "at": [6, 0, 6, 3],
      "series": [ { "topic": "loop.state", "field": "s" } ] },
    { "type": "slider", "title": "Kp",
      "action": "loop.config", "param": "kp" }
  ]
}
```

- **No TraceView:** quando o dispositivo tem `tv.dashboard`, aparece "Abrir dashboard do
  dispositivo", que cria uma área de trabalho. Editar gera uma cópia local, com a opção
  "Restaurar do dispositivo". Se a `revision` mudar, o usuário é avisado.
- Os campos sem estilo usam a configuração automática (§6, F3).

### 4.5 API do controlador portátil

O mesmo arquivo compila no TraceLab (DLL ou estático) e no firmware:

```c
typedef struct {
    uint32_t     tick;     /* loop.state.tick */
    float        dt;       /* período nominal, s */
    const float* y;        /* medições, na ordem do perfil */
    uint16_t     ny;
} loopctl_in;

typedef struct {
    float*   u;            /* u[k*nu + i], k = 0..horizon-1 */
    uint16_t nu;
    uint16_t horizon;      /* capacidade; o controlador devolve quantos passos escreveu */
} loopctl_out;

typedef struct {
    const char* name;
    const char* profile;   /* "lf" */
    int  (*init)(void** state, const loopctl_param* params, uint16_t nparams);
    int  (*step)(void* state, const loopctl_in* in, loopctl_out* out);
    void (*reset)(void* state);
    void (*destroy)(void* state);
} loopctl_controller;
```

- **Parâmetros (`loopctl_param`):** nome, valor, unidade e faixa. São os mesmos que
  `loop.config` expõe, o que dá ajuste ao vivo nos três estágios.
- **Sem alocação dentro de `step`:** é exigência do firmware (D9 da BTPDevice).

### 4.6 A flag `STREAM` (única mudança no BTP)

- **Onde fica:** é o bit 2 das action flags. A ação marcada com ela:
  - não gera `COMMAND_RESULT`;
  - não passa pela deduplicação;
  - não tem retry;
  - **vale a mais recente** (um slot por ação no executor);
  - tem prioridade de transmissão igual à do `COMMAND_REQUEST`.
- **O que vem de graça:** o schema tipado do manifest, ou seja, nome, unidade e
  faixa de `u_l`/`u_r`.
- **Por que não sem mudar o BTP:**
  - **comando:** o retry chega tarde, cada envio gera uma resposta (o que dobra o tráfego) e o
    cache de deduplicação enche;
  - **terminal:** são bytes sem schema;
  - **PC publicando telemetria:** inverte o modelo, e o robô teria que consumir manifest.
- **Versão:** a mudança é aditiva, então é MINOR. Um executor antigo rejeita a ação
  desconhecida, o que é o comportamento seguro.

---

## 5. Como os estágios usam a mesma língua

| | Estágio 1 | Estágio 2 | Estágio 3 |
|---|---|---|---|
| Quem publica `loop.state` | robô simulado | robô real | robô real |
| Quem manda `loop.u` | TraceLab | TraceLab | ninguém (`mode = ONBOARD`) |
| Onde roda `loopctl_controller` | TraceLab | TraceLab | firmware |
| Dashboard | `tv.dashboard` do simulado | `tv.dashboard` do robô (mesmo) | mesmo |
| O que o TraceView mostra | state/output/stats/truth | state/output/stats/pose | state/output/stats/pose |

---

## 6. Fases

Cada fase termina com revisão sua antes da próxima.

### F0: levantamento restante (sem código)

1. O schema de configuração dos widgets em `.tvproj` (`docs/DASHBOARD.md`), para
   basear o `tvdash/1`.
2. O caminho do `COMMAND_REQUEST` no `btp::Node`, para saber onde a flag `STREAM`
   evita a deduplicação e o result.
3. O limite de remontagem do TraceView para um `JSON_UTF8` de alguns KB
   fragmentado em ESP-NOW (frames de ~200 B).
4. A latência base por link (USB, TCP, BLE, hub ESP-NOW) com a ação de ping que já
   existe, a 80–100 Hz.
5. O que o bally_OS já publica e como mapeia para `loop.*` e `lf.*`.

**Aceite:** um anexo neste arquivo com os números e as respostas.

### F1: especificação (só documentação)

1. Escrever a especificação dos perfis `loop`, `lf` e `tv.dashboard` (§4) no
   lugar decidido (§8, item 1), com as regras gerais e a política do robô.
2. Escrever vetores de teste: manifests de exemplo e payloads de `loop.state` e
   `loop.u`, no mesmo padrão de `BTP/test-vectors/`.
3. Escrever o header `loopctl.h` (§4.5) e um header de nomes do perfil, com as
   constantes de tópico, campo e ordem. Esse header elimina a duplicação de hoje
   entre os `bally_channels.h`.

**Aceite:** a especificação revisada por você. Nada de código de produto.

### F2: BTP, flag `STREAM`

1. Escrita e leitura da flag no manifest.
2. Executor: slot "mais recente" por ação, sem deduplicação e sem result.
3. Testes, `docs/commands.md` e tag MINOR.

**Aceite:** os testes do BTP passam, e um executor antigo rejeita a ação sem quebrar.

### F3: TraceView, partes genéricas (úteis para qualquer hardware)

1. **Bind por nome:** novos `.tvproj` salvam (dispositivo, tópico, campo, elemento).
   Os antigos são migrados ao abrir, resolvendo os ids pelo catálogo.
2. **Configuração automática:** escolher um tópico cria o widget com todos os campos,
   eixo por unidade, faixa do min/max, enum como rótulos e array como barras.
   A configuração manual continua disponível.
3. **Widgets de controle a partir de ações tipadas:** a faixa, a unidade e a
   confirmação de ações `DANGEROUS` vêm do manifest, no lugar do texto cru.
4. **`tv.dashboard`:** importar de arquivo primeiro e depois do dispositivo, com cópia
   local e "Restaurar".

**Aceite:** o mesmo `.tvproj` abre e plota em dois dispositivos diferentes com os
mesmos nomes.

### F4: TraceLab, estágio 1

1. Infraestrutura de módulos (plano anterior: registro de módulos, liga/desliga,
   `--mode lab`).
2. Robô simulado como dispositivo BTP em loopback. Ele publica `loop.*`, `lf.*`,
   `lf.truth` e `tv.dashboard`, e aceita `loop.u`/`loop.mode`/`loop.config` com
   a mesma política do §4.2.
3. Host de controladores: carrega um `loopctl_controller`, roda a cada
   `loop.state` e manda `loop.u`.
4. Portar o primeiro controlador (PD, depois GPC/MPC) para `loopctl`.

**Aceite:** a simulação completa roda, e o TraceView puro (sem módulo) consegue plotar o
simulado conectando por TCP.

### F5: BTPDevice, suporte aos perfis

1. Ações tipadas no manifest (`c.action("loop.config").f32("kp", ...)`).
2. Entradas STREAM (`t.input("loop.u")...`) com slot "mais recente" lido pela task
   de controle.
3. Tópico documento (`JSON_UTF8`) para o `tv.dashboard`.
4. `btpdev::Loop`: a política do §4.2 (idade, manter, reserva, watchdog,
   estatísticas) pronta. **Todo firmware ganha a segurança sem reescrever.**
5. Exemplo `06_closed_loop`: uma planta de 1ª ordem simulada no próprio ESP32, para
   testar sem robô.

**Aceite:** o exemplo 06 fecha a malha com o TraceLab, e as estatísticas batem com as
perdas induzidas.

### F6: bally_OS, estágio 2

Depende da migração para a BTPDevice.

1. Publicar `loop.state`, `loop.output`, `loop.stats` e `lf.pose` e servir o
   `tv.dashboard`.
2. Modo REMOTE com `btpdev::Loop`.

**Aceite:** o mesmo projeto do estágio 1 roda no robô real só trocando o dispositivo.

### F7: estágio 3

1. Compilar o `loopctl_controller` no firmware (modo ONBOARD), com os parâmetros
   expostos por `loop.config`.

**Aceite:** o mesmo controlador, em código idêntico, roda no TraceLab e no robô.

---

## 7. Riscos

| Risco | Mitigação |
|---|---|
| Latência do link maior que o período da malha (BLE no limite a 80 Hz) | Medição na F0. Horizonte M cobre perdas. As estatísticas mostram a idade real |
| Horizonte grande demais para um frame ESP-NOW | ~8 B por passo para 2 entradas: M ≈ 20 cabe num frame. Validar na F0 |
| `tv.dashboard` grande fragmentado em ESP-NOW | Verificar a remontagem na F0. Manter o template enxuto (bind por nome, sem estilo obrigatório) |
| Perfis mudando a toda hora | Evolução só aditiva (D4). Quebra exige nome novo |
| Stream mexer no motor sem querer | Só em REMOTE/HIL e armado por ação `DANGEROUS` confirmada. Watchdog no robô (D6) |
| Migração dos `.tvproj` por id quebrar dashboards existentes | A migração resolve pelo catálogo ao abrir e mantém os ids como fallback até o dispositivo conectar |
| Especificação duplicada entre repositórios (como o `bally_channels.h` hoje) | Um único header de perfil num repositório que todos já usam (§8, item 1) |

---

## 8. Decisões em aberto

1. **Onde mora a especificação dos perfis e o `loopctl.h`?** Recomendo
   `BTP/profiles/`, como alvo header-only `btp::profiles`, separado da lib
   principal. O TraceView e a BTPDevice já dependem do BTP com tag fixa, então
   ganham os perfis sem uma dependência nova.
2. **Política de reserva padrão quando o link cai:** manter → controle embarcado →
   parar? Ou manter → parar até existir um controle embarcado?
3. **Taxa da malha alvo:** 80 Hz, como no simulador Python, ou outra?
4. **Migração automática dos `.tvproj` existentes** para bind por nome (F3.1), ou só
   para os projetos novos?

---

## 9. Decisões da revisão (2026-10-01)

1. **A especificação fica no BTP, como convenção.** O texto deixa claro que os
   perfis não fazem parte do protocolo e que o BTP pode ser usado de outra forma.
   O BTP é independente: **nada de seguidor de linha ou TraceView no BTP**. O
   perfil `loop` e a API `loopctl` são genéricos. O perfil `lf` e o formato do
   `tv.dashboard` ficam fora do BTP (TraceLab e TraceView, respectivamente).
2. **A política do robô sem sinal fica para o futuro.** A especificação descreve
   a correlação (`tick`/`ref_tick`) e reserva o espaço da política (manter,
   reserva, parar, watchdog), mas não a torna obrigatória agora.
3. **Os `.tvproj` antigos são convertidos** para bind por nome ao abrir.
4. **Achado da implementação:** o `btp::Catalog` ainda não guarda registros de
   ação (só tópicos). Para que um dispositivo declare ações tipadas e `STREAM`
   no manifest, o Catalog ganha suporte a ações na mesma versão da flag. O
   `btp::Node` passa a saber se uma ação é stream pela própria declaração no
   catálogo que serve, sem gancho extra.
