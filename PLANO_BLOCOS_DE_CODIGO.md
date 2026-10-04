# Plano: blocos de código no TraceLab

Criado: 2026-10-04
Repositório: TraceView (pasta `lab/`)
Estado: **esboço para discussão**. Nenhum código escrito. Complementa o
[PLANO_TRACELAB.md](PLANO_TRACELAB.md) e muda as decisões L6 e L10 (§9).

---

## 1. A ideia

Planta, filtro e controlador deixam de ser classes C++ fixas do benchmark e
viram **arquivos `.c` que você abre, edita, salva e recompila dentro do
TraceView**. Cada bloco escreve só a parte dele (a dinâmica, a lei de
controle, o filtro). O TraceLab cuida do resto: relógio, solver, BTP,
manifest e a ligação entre os blocos.

- Os cards continuam, mas só para **escolher e ver o estado** de cada bloco.
- Clicar num card abre o **código** do bloco, com os parâmetros ao lado.
- Cada benchmark traz **templates** (somente leitura). Você parte de um
  template ou de um esqueleto vazio, salva onde quiser e pode **voltar ao
  original** a qualquer momento.
- A cena animada do seguidor de linha continua específica do benchmark.

---

## 2. Por que hoje parece tudo amarrado

As camadas genéricas do `lab/` existem, mas estão vazias (`ScriptControllerAdapter`,
`DllControllerAdapter`, `SimulatedPlant`, `compileModel`, solver, agendador). O
seguidor de linha funciona por fora delas:

| Arquivo | O que mistura |
|---|---|
| `benchmarks/linefollower/device/lfplantdevice.cpp` (749 linhas) | relógio, modos de execução, planos do `loop.u`, BTP, manifest, dinâmica, sensores, ruído |
| `benchmarks/linefollower/device/lfcontrolleradapter.cpp` | `if` entre qLPV/GPC/PD, filtro embutido, nomes de sinais fixos (`w_l`, `s[i]`) |
| `module/labpanel.cpp` | cada campo de cada controlador escrito à mão |

Uns 80% do `lfplantdevice.cpp` são genéricos (relógio, planos, BTP). É isso
que sai do benchmark e vai para o núcleo.

---

## 3. Como o dado anda de um bloco para o outro

### 3.1 O caminho, por tick

```
        ┌──────────── processo da planta ────────────┐        ┌──────── host do controlador ─────────┐
        │                                            │        │                                      │
loop.u ─┼─► u_k (ZOH) ─► solver ─► plant_deriv()     │        │  tabela de sinais (por nome)         │
  ▲     │                  │       (N subpassos)     │        │   w_l  w_r  s[0..7]                  │
  │     │                  ▼                         │ BTP    │      │                               │
  │     │             plant_output() ──► loop.state ─┼───────►│      ▼                               │
  │     │                  │             sim.preview │        │  filtro 1 ─► filtro 2 ─► ...          │
  │     │                  ▼                         │        │      │   (nome igual substitui,      │
  │     │             sim.truth (estados)            │        │      │    nome novo acrescenta)      │
  │     └────────────────────────────────────────────┘        │      ▼                               │
  │                                                           │  ctl_step()  ─► u_l, u_r [horizonte] │
  └───────────────────────────── loop.u (STREAM) ◄────────────┼──────┘                               │
                                                              │  ctl.diag ─► dashboards e cena       │
                                                              └──────────────────────────────────────┘
```

1. A planta aplica `u_k`, que vem do plano mais novo do `loop.u` (é o que já
   acontece hoje). Integra `plant_deriv()` por um período `Ts`, em subpassos
   do solver.
2. Chama `plant_output()` e publica `loop.state` com as saídas declaradas,
   `sim.preview` se o ambiente tiver referência, e `sim.truth` com os estados.
3. O host do controlador recebe `loop.state` e monta uma **tabela de sinais
   por nome**.
4. Os **filtros** rodam em ordem sobre essa tabela. O filtro fica do lado do
   controlador porque no robô real ele roda no firmware, antes do controle.
5. O controlador lê os sinais que declarou, calcula e escreve as entradas
   (o horizonte inteiro, no caso do MPC).
6. As entradas voltam como **um** pacote `loop.u` STREAM, e `ctl.diag` vai
   para os dashboards.

**Entre a planta e o controlador continua sendo BTP** (decisão L4). É isso que
permite, no estágio 2, trocar a planta simulada pelo robô real sem mexer no
controlador nem nos filtros. Os filtros e o controlador rodam no mesmo host,
por chamada direta, porque no robô eles também rodam juntos.

### 3.2 A ligação é por nome

Cada bloco **declara** o que lê e o que escreve. O TraceLab liga por nome:

| A planta declara | vira no BTP | o controlador/filtro usa |
|---|---|---|
| `TL_OUTPUT(w_l, "rad/s")` | campo `w_l` de `loop.state` | `TL_IN(w_l)` |
| `TL_OUTPUT_ARRAY(s, 8, "")` | campo `s[8]` de `loop.state` | `TL_IN_ARRAY(s, 8)` |
| `TL_INPUT(u_l, "%", -100, 100)` | parâmetro `u_l` do `loop.u` (min/max saturam) | `TL_OUT(u_l)` ou `TL_OUT_PLAN(u_l, M)` |
| `TL_STATE(th, "rad", 0)` | `sim.truth` / `sim.states` | `TL_TRUTH(th)` (privilegiado, ver §3.3) |
| ambiente `track` | `sim.preview` | `TL_PREVIEW(px, py, N)` (privilegiado) |

Se um nome não existe, o card do bloco fica vermelho com a lista do que existe
(*"`w_l_f` não existe. Tem: `w_l`, `w_r`, `s[0..7]`"*). Nesse caso o
controlador não manda `loop.u`, e a planta segura a última entrada, como já
acontece hoje.

**Filtros:** se a saída tem um nome que já existe, ela substitui o sinal; se o
nome é novo, ela acrescenta. Com `TL_OUT(w_l)`, o controlador passa a ver o
`w_l` filtrado sem mudar uma linha; tirou o filtro, ele volta a ver o cru. Com
`TL_OUT(w_l_f)`, ele vê os dois.

### 3.3 Informação privilegiada (regra L5)

`TL_TRUTH` e `TL_PREVIEW` funcionam, mas o card do controlador ganha a
etiqueta **"usa dados da simulação"**. Assim fica sempre visível que aquele
controlador não roda igual no robô real.

---

## 4. Os três contratos

Todo arquivo começa com `#include <tracelab.h>`. As declarações (`TL_*`) viram
variáveis C normais, que o TraceLab preenche antes de chamar sua função e lê
depois. Dentro do código, você usa `w_l`, `kp` e `u_l` como variáveis comuns.

### 4.1 Planta

```c
#include <tracelab.h>

TL_PARAM(r,     0.04,  "m",     0.01, 0.2);
TL_PARAM(L,     0.15,  "m",     0.05, 0.5);
TL_PARAM(tau_l, 0.124, "s",     0.01, 1.0);
TL_PARAM(tau_r, 0.116, "s",     0.01, 1.0);
TL_PARAM(wmax,  104.7, "rad/s", 1,    500);

TL_INPUT(u_l, "%", -100, 100);
TL_INPUT(u_r, "%", -100, 100);

TL_STATE(wl, "rad/s", 0);
TL_STATE(wr, "rad/s", 0);
TL_STATE(x,  "m",     0);
TL_STATE(y,  "m",     0);
TL_STATE(th, "rad",   0);

TL_OUTPUT(w_l, "rad/s");
TL_OUTPUT(w_r, "rad/s");
TL_OUTPUT_ARRAY(s, 8, "");

void plant_deriv(void) {               /* chamado pelo solver, várias vezes por tick */
    double v = r * (wl + wr) / 2;
    der(wl) = (wmax * u_l / 100 - wl) / tau_l;
    der(wr) = (wmax * u_r / 100 - wr) / tau_r;
    der(x)  = v * cos(th);
    der(y)  = v * sin(th);
    der(th) = r * (wl - wr) / L;
}

void plant_output(void) {              /* uma vez por tick, depois do solver */
    w_l = tl_quantize(wl, 2 * M_PI / 1400 * tl_fps());   /* encoder: ticks inteiros */
    w_r = tl_quantize(wr, 2 * M_PI / 1400 * tl_fps());
    tl_track_sensors(x, y, th, s);     /* ambiente "pista", nativo */
    for (int i = 0; i < 8; i++) s[i] += tl_noise(0.02);
}
```

- `der(x)` é a derivada de `x`. Numa planta discreta (`TL_DISCRETE;` no topo),
  você escreve `next(x) = ...` e o solver não entra (é o que reproduz o
  simulador Python passo a passo).
- O solver, o passo e o modo de execução são configurados no card da
  planta, não no código.
- **Ambiente:** o que não é equação (geometria da pista, leitura dos sensores,
  progresso na volta, preview) continua nativo em C++ e aparece no código
  como funções `tl_track_*`. A pista vem do `.tfg` escolhido no card.
- Ruído e quantização ficam em `plant_output()`, com `tl_noise()` e
  `tl_quantize()`. A semente é controlada pelo TraceLab, então o resultado é
  reprodutível.

### 4.2 Filtro

```c
#include <tracelab.h>

TL_PARAM(tau, 0.02, "s", 0, 1);

TL_IN(w_l);   TL_IN(w_r);
TL_OUT(w_l);  TL_OUT(w_r);       /* mesmo nome: substitui o sinal */

static double fl, fr;

void filt_reset(void) { fl = fr = 0; }

void filt_step(void) {
    double a = tau > 0 ? tl_ts() / (tau + tl_ts()) : 1;
    fl += a * (in(w_l) - fl);
    fr += a * (in(w_r) - fr);
    out(w_l) = fl;
    out(w_r) = fr;
}
```

Quando o nome de entrada e o de saída coincidem, você usa `in(nome)` e
`out(nome)` para separar os dois.

### 4.3 Controlador

```c
#include <tracelab.h>

TL_PARAM(kp,   1200, "%/m",  0,    1e5);
TL_PARAM(kd,     40, "%s/m", 0,    1e4);
TL_PARAM(base,   60, "%",   -100,  100);

TL_IN_ARRAY(s, 8);
TL_OUT(u_l);  TL_OUT(u_r);
TL_DIAG(e, "m");

static double e_prev;

void ctl_reset(void) { e_prev = 0; }

void ctl_step(void) {
    e = tl_line_offset(s, 8);          /* da lib do benchmark */
    double de = (e - e_prev) / tl_ts();
    e_prev = e;
    double w = kp * e + kd * de;
    u_l = base - w;
    u_r = base + w;
}
```

- **MPC:** `TL_OUT_PLAN(u_l, 10)` vira `double u_l[10]`, e o plano inteiro vai
  no `loop.u`. Para a cena, `tl_predicted(px, py, th, n)` publica a trajetória
  prevista.
- `TL_DIAG` publica a variável em `ctl.diag`, e ela já aparece nos gráficos.
- O estado interno (`static`) é zerado em `ctl_reset()`, que roda no reset da
  simulação e depois de cada recompilação.

### 4.4 Biblioteca padrão (`tracelab.h`)

O pesado é **compilado de verdade, com otimização**, e exposto ao código. O
seu arquivo fica só com a lógica.

| Grupo | Funções |
|---|---|
| Tempo | `tl_ts()`, `tl_fps()`, `tl_tick()`, `tl_time()` |
| Matriz | `tl_mat` (denso pequeno): mul, transposta, solve, Cholesky |
| Otimização | `tl_qp_box()` (o `boxqp` atual, portado para C) |
| Filtros | o `tlfilter.c` que já existe |
| Sensores | `tl_noise()`, `tl_quantize()`, `tl_deadzone()`, `tl_sat()` |
| Ambiente pista | `tl_track_sensors()`, `tl_track_preview()`, `tl_line_offset()` |
| Diagnóstico | `tl_predicted()`, `tl_log()` (vai para o console do TraceLab) |

---

## 5. Compilação: C com libtcc

O código é **C**, compilado dentro do TraceView com a **libtcc** (Tiny C
Compiler, embutida, LGPL e ligada dinamicamente).

1. Ao salvar (Ctrl+S), o TraceLab compila o arquivo em memória
   (`tcc_compile_string`). Leva poucos milissegundos.
2. As funções da biblioteca padrão entram por `tcc_add_symbol`, então não há
   nada para linkar.
3. As declarações `TL_*` geram, além da variável, um descritor estático com
   prefixo fixo. O TraceLab lista esses símbolos depois de compilar e sabe
   quais são as entradas, saídas, parâmetros e diagnósticos, sem precisar
   parsear o texto.
4. **A troca acontece entre dois ticks.** Se a compilação falhar, a versão
   anterior continua rodando, e os erros aparecem no editor com a linha.
5. Planta recompilada reinicia a simulação. Filtro ou controlador recompilado
   só chama o próprio `*_reset()`.

Por que C: o mesmo arquivo do controlador compila no ESP32 (estágio 3). O
`tracelab.h` do firmware implementa as mesmas declarações em cima do
`loopctl.h`.

---

## 6. Arquivos, templates e "voltar ao original"

- **Templates** ficam nos recursos do benchmark, somente leitura:
  `:/tracelab/templates/linefollower/{plant,filters,controllers}/*.c`.
- **Seus arquivos** ficam em disco, numa pasta escolhida por você (por padrão
  `Documentos/TraceLab/`). O `.tvproj` guarda o **caminho** de cada bloco e os
  valores dos parâmetros.
- **Ao abrir um template**, o TraceLab cria uma cópia na sua pasta. A primeira
  linha registra a origem:

  ```c
  // tracelab: template linefollower/controllers/pd.c
  ```

- **Voltar ao original** relê esse template e sobrescreve o arquivo (pede
  confirmação). O card mostra a etiqueta "modificado" enquanto o arquivo for
  diferente do template.
- **Novo** cria um arquivo com o esqueleto do tipo de bloco (as funções
  vazias e um exemplo de cada declaração).
- Se o arquivo mudar fora do TraceView (VS Code, por exemplo), ele recompila
  sozinho.

---

## 7. A tela

```
┌─ TraceLab ──────────────────────────────────────────────────────────────────────┐
│ [▶] [⏸] [⟲] [⏭]   80 Hz · tempo real · t = 12,4 s                                 │
├─────────────────────────────────────────────────────────────────────────────────┤
│ ┌ PLANTA ─────────┐   ┌ FILTRO ─────────┐   ┌ + filtro ┐   ┌ CONTROLADOR ─────┐   │
│ │ lf_plant.c      │ → │ lowpass.c       │ → │          │ → │ meu_pd.c          │   │
│ │ ● compilado     │   │ ● compilado     │   └──────────┘   │ ● modificado      │   │
│ └─────────────────┘   └─────────────────┘                  └───────────────────┘   │
├──────────────────────────────────────────────────────┬──────────────────────────┤
│ meu_pd.c   [template ▾] [Voltar ao original] [Salvar] │ Parâmetros               │
│ ──────────────────────────────────────────────────── │  kp    [ 1200 ]          │
│  1  #include <tracelab.h>                            │  kd    [   40 ]          │
│  2                                                   │  base  [   60 ]          │
│  3  TL_PARAM(kp, 1200, "%/m", 0, 1e5);               │ Sinais disponíveis       │
│  …                                                   │  w_l  rad/s  12,31 (filt)│
│                                                      │  s[8]        0,0 … 0,9   │
│ ✓ compilado em 8 ms                                  │  u_l  %      (saída)     │
└──────────────────────────────────────────────────────┴──────────────────────────┘
```

- **Cards:** escolher, adicionar filtro, ver se compilou ou se tem erro. O
  card da planta também tem a pista, o solver e a taxa.
- **Editor:** com destaque de sintaxe, número de linha e erro na linha.
  Clicar num sinal da lista insere o `TL_IN(...)` correspondente.
- **Parâmetros:** gerados dos `TL_PARAM` e alterados ao vivo, sem
  recompilar. O valor fica no projeto. O do código é só o padrão.

---

## 8. O que muda no código do TraceView

| Hoje | Depois |
|---|---|
| `LinePlantDevice` (tudo junto) | `lab/plant/plantdevice` genérico (relógio, modos, planos, BTP) + `CodePlant` (estados, solver, saídas) + manifest gerado das declarações |
| `lfplantmanifest.cpp` | `lab/plant/plantmanifest.cpp`, gerado do `CodePlant` |
| `LineControllerAdapter` + `Filter` embutido | `CodeControllerAdapter` + `FilterChain` dentro do `ControllerHost`, ligados por nome |
| `qlpvcontroller`, `gpccontroller`, `pdcontroller` (C++) | templates `.c` (removidos depois de validados contra os nativos) |
| `labpanel.cpp` com campos à mão | painel genérico: cards, editor, parâmetros e sinais lidos das declarações |
| `LineDeviceRegistry` | `LabDeviceRegistry` genérico (plantas e hosts) |
| — | `lab/code/`: wrapper da libtcc, `tracelab.h`, lib padrão, leitura dos descritores |

O que **fica no benchmark** `linefollower/`: o ambiente da pista (geração,
`.tfg`, sensores, progresso), a `DiffDriveScene`, os templates `.c` e o
projeto de exemplo.

---

## 9. Decisões que mudam

| # | Antes | Agora |
|---|---|---|
| L6 | Modelo em `.tlmodel` com equações em texto (ExprTk) | Planta em **C** (`plant_deriv`/`plant_output`). O ExprTk deixa de ser necessário |
| L8 | Controlador por DLL, JS ou processo externo | **C compilado na hora (libtcc)** é o caminho principal. O processo externo via BTP continua possível para o MPC Python |
| L10 | O `.tvproj` não carrega código | O `.tvproj` aponta para arquivos `.c`. Abrir um projeto de outra pessoa pede confirmação antes de compilar ("este projeto usa código de …, confiar?") |

---

## 10. Fases

Cada fase termina com o seguidor de linha funcionando.

| Fase | O quê |
|---|---|
| **B0** | Commitar o estado atual do `feature/tracelab-sensors-filters` |
| **B1** | Desamarrar: `PlantDevice` genérico, `FilterChain` e ligação por nome no `ControllerHost`, `LabDeviceRegistry`. Tudo ainda nativo em C++ |
| **B2** | `lab/code/`: libtcc, `tracelab.h`, descritores, `CodeControllerAdapter` com o PD em C. Validar contra o PD nativo |
| **B3** | Editor no painel: cards, código, parâmetros, sinais, templates, salvar e voltar ao original |
| **B4** | Filtro em código (passa-baixa) e `FilterChain` com blocos de código |
| **B5** | Planta em código: `CodePlant`, Euler/RK4, manifest gerado, ambiente pista. Regressão contra a planta nativa |
| **B6** | Biblioteca: `tl_mat` e `tl_qp_box` em C. Portar o GPC e o qLPV para templates e validar contra os nativos e os CSVs do Python |
| **B7** | Remover os controladores e a planta nativos do benchmark |

---

## 11. Riscos

| Risco | Mitigação |
|---|---|
| libtcc no macOS arm64 é menos madura | Medir cedo (B2). Se falhar, cair para o compilador do sistema gerando DLL no macOS |
| Código do usuário trava (loop infinito) ou derruba o processo | Rodar com limite de tempo por tick e avisar. Proteção total exigiria processo separado; fica para depois, se incomodar |
| libtcc gera código mais lento que gcc `-O2` | O pesado (QP, matriz) é da lib nativa. Medir o qLPV iterativo em B6: precisa caber em 12,5 ms |
| Portar o qLPV muda o resultado | Validar cada template contra o nativo, tick a tick, antes de remover o nativo (B7) |
| Projeto enviado sem os `.c` | O card mostra "arquivo não encontrado" com o caminho. Um "exportar projeto com código" pode entrar depois |

---

## 12. Em aberto

1. **Mais de uma malha por projeto** (duas plantas com controladores
   diferentes lado a lado)? O esboço supõe uma malha por vez.
2. **Solvers:** Euler e RK4 bastam por agora, ou já entra um adaptativo?
3. **Editor:** um `QPlainTextEdit` com destaque simples basta, ou vale usar
   um editor de verdade (QScintilla, que é GPL ou comercial)?
