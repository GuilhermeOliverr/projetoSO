# Plano do Projeto A: Simulador de Escalonamento (RM / EDF)

Este documento resume o enunciado (`simulador-escalonamento-v20260830-enunciado.pdf`, v0.6)
e o capítulo de referência (`2-Sistemas de Tempo Real - Capitulo 2`) e define como
vamos implementar o **Projeto A**. O Projeto B ainda não foi divulgado; o plano só
deixa ganchos para ele (eventos, mutex, aperiódicas).

---

## 1. Resumo dos requisitos

### Gerais (valem para A e B)
| # | Requisito | Impacto no projeto |
|---|-----------|--------------------|
| G1 | SO multitarefa **preemptivo** de tempo compartilhado | escalonador roda a cada tick |
| G2 | **1 a N CPUs**, configurável | arrays dimensionados em tempo de execução |
| G3 | Visualização gráfica por tarefa **e por processador**, com legenda | Gantt na tela + export de imagem |
| G4 | Avançar **e retroceder**; editar tarefas em qualquer passo | histórico de snapshots |
| G5 | Rodar **sem instalar nenhuma biblioteca/runtime extra** | só C11 + libc; nada de SDL/GTK/ncurses |
| G6 | Código comentado, com o **porquê** das decisões | padrão já usado em `main.c` |

### Projeto A
- **Relógio global em ticks**; a cada tick verifica eventos (chegadas, término, prazo).
- **Fila de prontos global**; nenhuma CPU ociosa se houver tarefa pronta. CPU sem tarefa
  fica **desligada**, e isso aparece para o usuário e no gráfico.
- Todas as informações de uma tarefa num único **TCB**.
- Modos: **(a) passo a passo** (debugger: inspecionar sistema/tarefas, avançar, retroceder,
  editar) e **(b) execução completa** (só o resultado final).
- **Gantt** (estilo seção 6.4 do Maziero):
  - executando = cor da tarefa (vinda do arquivo); pronta = sem cor;
    suspensa = **preto com preenchimento diferente** (pontilhado);
  - marcadores de **chegada**, **término**, **perda de prazo** e **sorteio** no tick exato;
  - eixo Y: **menor ID embaixo**, IDs crescentes para cima;
  - indicar **qual CPU** executou cada trecho;
  - atualizado a cada passo no modo (a);
  - ao final, **exportar imagem** (PNG/SVG…) com a simulação inteira, sem limite de tempo
    e que **não seja print de tela**.
- **Configuração** por arquivo texto (qualquer nome/caminho), com valores padrão
  sugeridos e editáveis antes/durante a simulação.
- **Algoritmos**: `RM` e `EDF`, ambos preemptivos, plugáveis sem mexer na simulação.
- **Desempate**, nesta ordem: (1) tarefa que já estava executando; (2) prazo;
  (3) ingresso mais antigo; (4) menor duração; (5) **sorteio**, com marcador no Gantt.
- Somente **tarefas periódicas**: `periodo == 0` (aperiódica) é ignorada **com aviso**.
  Cada tarefa periódica termina após **10 ativações**.
- Perda de prazo: a tarefa **continua executando** até completar a duração; o erro aparece no Gantt.
- Qualquer erro deve ser mostrado de forma clara, com o motivo.

---

## 2. Formato do arquivo de configuração

```
algoritmo_escalonamento;quantum;qtde_cpus
id;cor;ingresso;duracao;periodo;prazo;lista_eventos
...
```

| Campo | Tipo | Validação |
|-------|------|-----------|
| `algoritmo_escalonamento` | string | `RM` ou `EDF`, sem diferenciar maiúsculas/minúsculas |
| `quantum` | int | > 0 |
| `qtde_cpus` | int | 1..N |
| `id` | int | único |
| `cor` | hex `RRGGBB` | 6 dígitos hex; aceitar com ou sem `#` |
| `ingresso` | int | >= 0 |
| `duracao` | int | > 0 |
| `periodo` | int | > 0 periódica; = 0 aperiódica (ignorada com aviso); < 0 **erro** |
| `prazo` | int | > 0, relativo à ativação |
| `lista_eventos` | texto | guardado cru para o Projeto B (pode ter vários eventos) |

Regras do parser:
- linha pode terminar com `;` ou não;
- espaços e linhas em branco são ignorados;
- mínimo de 2 linhas válidas (sistema + 1 tarefa), sem limite máximo de tarefas;
- erros indicam **arquivo, linha, campo e motivo** (ex.: `config.txt:4: campo 'periodo' = -3: valor negativo não é permitido`);
- **falhar ao carregar um arquivo válido inviabiliza a defesa**, então o parser precisa de
  uma bateria de arquivos de teste (válidos e inválidos).

Exemplo (tabela 2.1 do capítulo, com D = P):
```
RM;2;1
1;E74C3C;0;20;100;100
2;3498DB;0;40;150;150
3;2ECC71;0;100;350;350;
```

---

## 3. Teoria necessária (capítulo 2)

**Modelo de tarefa periódica** (Ci = duração, Pi = período, Di = prazo relativo):
- ativação k (k = 0..9) chega em `ingresso + k·Pi`;
- deadline absoluto da ativação k: `ingresso + k·Pi + Di`.

**Rate Monotonic (2.4.1)**: prioridade **fixa**, e quanto **menor o período, maior a
prioridade**. Teste suficiente: `U = Σ Ci/Pi ≤ n(2^(1/n) − 1)` (tende a 0,69).

**EDF (2.4.2)**: prioridade **dinâmica**; executa quem tem o **deadline absoluto mais
próximo**. Com D = P, é escalonável se e somente se `U ≤ 1`.

Com M CPUs (escalonamento global) a escolha a cada tick é: **as M tarefas prontas de
maior prioridade**. Os testes acima são de monoprocessador; vamos mostrar a utilização
`U` como informação, sem usá-la para bloquear a simulação.

**Casos de validação** (o simulador deve reproduzir):
- Fig. 2.5: tabela 2.1 sob RM, com A, B, C chegando em t = 0. A roda até 20, B até 60,
  C começa em 60 e é preemptada em 100 por A, depois por B (150) e A (200).
- Fig. 2.6: A (C=10, P=20) e B (C=25, P=50), U = 100%. EDF não perde prazo;
  RM faz **B perder o deadline em t = 50**.

---

## 4. Decisões de arquitetura

### 4.1 Linguagem e interface
- **C11 + libc**, como já está no repositório (Makefile e README).
- **Interface no terminal com códigos ANSI** (cores 24-bit). Assim não precisa de
  biblioteca gráfica e o requisito G5 fica atendido.
- **Imagem final em SVG**, gerada à mão (é só texto). Não precisa de lib, não tem limite
  de tamanho e abre em qualquer navegador.
- **Opcional**: também gerar um `.html` com o SVG e controles para navegar pelos ticks.

### 4.2 Módulos (arquivos na raiz, como o Makefile já espera)
```
main.c          # menu principal e argumentos de linha de comando
config.c/.h     # leitura e validação do arquivo de configuração e valores padrão
task.h          # TCB e estados da tarefa
sim.c/.h        # relógio, ativações, execução por tick, detecção de eventos
sched.h         # interface do escalonador (contrato dos plugins)
sched_rm.c      # Rate Monotonic
sched_edf.c     # Earliest Deadline First
sched_tie.c     # cadeia de desempate comum (critérios 1 a 5)
history.c/.h    # snapshots para avançar/retroceder
gantt_tui.c     # desenho do Gantt no terminal
gantt_svg.c     # exportação SVG
ui.c/.h         # modo passo a passo: comandos, inspeção e edição
tests/          # arquivos .txt de configuração (válidos e inválidos) + script de teste
```

### 4.3 TCB
```c
typedef enum { T_NOVA, T_PRONTA, T_EXECUTANDO, T_SUSPENSA, T_TERMINADA } EstadoTarefa;

typedef struct {
    /* definidos no arquivo */
    int id; unsigned cor; int ingresso, duracao, periodo, prazo;
    char *eventos_raw;          /* Projeto B */
    /* estado dinâmico */
    EstadoTarefa estado;
    int ativacao;               /* 0..9 */
    int restante;               /* ticks restantes na ativação atual */
    int chegada_atual;          /* instante da ativação atual */
    int deadline_abs;           /* chegada_atual + prazo */
    int cpu;                    /* -1 se não está executando */
    int quantum_usado;
    bool prazo_perdido;
    /* estatísticas */
    int ticks_exec, ticks_espera, deadlines_perdidos;
} TCB;
```

### 4.4 Escalonador plugável
```c
/* Compara duas tarefas prontas: <0 se a tem prioridade maior que b, 0 se empata. */
typedef int (*prioridade_fn)(const TCB *a, const TCB *b, int agora);

typedef struct {
    const char *nome;           /* "RM", "EDF" (comparação case-insensitive) */
    prioridade_fn prioridade;
} Escalonador;
```
- A simulação ordena a fila de prontos com `prioridade` e, quando dá empate, aplica a
  **cadeia de desempate** (`sched_tie.c`). Depois atribui as M primeiras às CPUs.
- Um novo algoritmo é só uma struct a mais numa tabela de registro.
- **Extra** (sugerido no item 4.2 do enunciado): carregar um `.so` com `dlopen`, que faz
  parte da libc, então não fere G5.
- Sorteio com `rand()` e semente registrada no snapshot, para que o retroceder/avançar
  seja **determinístico**.

### 4.5 Ciclo de um tick `t`
1. **Chegadas**: tarefas com ativação em `t` vão para PRONTA (evento *chegada*).
   Tarefas com `periodo == 0` já foram descartadas no carregamento, com aviso.
2. **Escalonamento**: ordena as prontas e executando e escolhe as M primeiras; as outras
   voltam para PRONTA (preempção). Empate resolvido por sorteio gera evento *sorteio*.
3. **Execução**: cada CPU ocupada decrementa `restante` da sua tarefa; CPU sem tarefa
   fica marcada **desligada** em `t`.
4. **Verificações**: `restante == 0` gera evento *fim da ativação* (e *término* na 10ª);
   `t+1 > deadline_abs` com `restante > 0` gera evento *perda de prazo* (uma vez por ativação).
5. Grava a coluna `t` do Gantt e o snapshot; `t++`.
6. A simulação acaba quando todas as tarefas estão TERMINADAS.

### 4.6 Histórico (retroceder/avançar)
- Um **snapshot completo** por tick: TCBs, CPUs, fila, semente do RNG e eventos.
  É simples e correto; o custo de memória é baixo para o tamanho do projeto.
- Retroceder = carregar o snapshot `t−1`. Avançar = reaplicar o snapshot gravado, ou
  simular se ainda não existe.
- **Edição em um passo passado** descarta os snapshots futuros e a simulação segue a
  partir do estado editado (como num debugger).
- Edições inválidas (ex.: `restante` negativo, CPU inexistente, ativar tarefa terminada)
  são recusadas **com o motivo**.

### 4.7 Gantt
- **Linhas por tarefa** (ID maior em cima, menor embaixo, junto do eixo X), mais um
  bloco de **linhas por CPU** (qual tarefa ocupa cada CPU ou `desligada`).
- Célula executando: cor da tarefa com o número da CPU dentro (`0`, `1`…).
  Pronta: vazia. Suspensa: preto pontilhado (`░` no terminal, `pattern` no SVG).
- Marcadores: `↓` chegada, `↑`/`■` término, `✗` perda de prazo, `?` sorteio.
- **Legenda** sempre visível. Em terminal, rolagem horizontal quando o tempo não cabe na tela.

### 4.8 Interface (modo passo a passo)
```
[n] próximo   [p] anterior   [g t] ir para tick t   [r] rodar até o fim
[s] estado do sistema   [t id] detalhes da tarefa   [e id] editar tarefa
[a] trocar algoritmo   [x] exportar SVG   [q] sair
```
Na inicialização: carregar arquivo (caminho digitado ou passado como argumento),
**mostrar os parâmetros com valores padrão**, permitir editar e escolher o modo (a/b).

---

## 5. Questões em aberto (confirmar com o professor)
1. **Quantum em RM/EDF**: proposta: usar o quantum só para revezar tarefas de **mesma
   prioridade** (quando o desempate cai no critério 1, rodízio ao estourar o quantum).
2. **Nova ativação chegando enquanto a anterior ainda não terminou** (prazo > período ou
   atraso): proposta: acumular (a próxima ativação começa logo que a anterior termina)
   e contar como ativação.
3. **"Ingresso"** = instante da 1ª ativação (o capítulo assume t = 0; o arquivo permite outro valor).
4. Em Projeto A não existem eventos de suspensão; o estado SUSPENSA só aparece por
   edição manual. Confirmar se isso basta.
5. Valor máximo de N para `qtde_cpus` (proposta: 64).

---

## 6. Etapas de implementação
| Etapa | Entrega | Como verificar |
|-------|---------|----------------|
| 1 | `task.h`, `config.c` + arquivos de teste | todos os arquivos válidos carregam; inválidos dão erro com linha e motivo |
| 2 | `sim.c` + `sched_rm.c` em modo completo, saída em texto | reproduz a Fig. 2.5 |
| 3 | `sched_edf.c` + `sched_tie.c` | reproduz a Fig. 2.6 (EDF sem perda; RM perde B em t = 50) |
| 4 | Múltiplas CPUs + CPU desligada | casos com 2 e 3 CPUs, verificados à mão |
| 5 | `gantt_svg.c` | SVG abre no navegador; legenda, marcadores e ordem do eixo Y corretos |
| 6 | `gantt_tui.c` + `ui.c` (passo a passo) | navegação, inspeção |
| 7 | `history.c` + edição de tarefas | avançar/retroceder idempotente; edição descarta o futuro |
| 8 | Polimento: mensagens de erro, README, comentários | revisão contra a checklist da seção 1 |
| 9 | (extra) plugin `.so` via `dlopen` e/ou visualizador HTML | — |

---

## 7. Checklist para a defesa
- [x] Carrega qualquer arquivo válido, de qualquer caminho, sem diferenciar maiúsculas/minúsculas, com ou sem `;` final
- [x] Erros de arquivo com motivo claro; aperiódicas ignoradas com aviso
- [x] RM e EDF preemptivos; desempate nos 5 critérios; marcador de sorteio
- [x] 1..N CPUs, fila global, CPU desligada visível
- [x] Passo a passo: inspecionar, avançar, retroceder, editar (com validação)
- [x] Execução completa
- [x] Gantt: cores, suspensa preto pontilhado, chegada, término, prazo perdido, CPU, legenda, menor ID embaixo
- [x] SVG final da simulação inteira
- [x] 10 ativações por tarefa periódica
- [x] Compila com `make` e roda sem dependências extras
- [x] Código comentado com justificativas
