#!/bin/sh
# run_tests.sh - bateria de testes do simulador (rode com `make test`).
#
# Por que existe: o enunciado diz que falhar ao carregar um arquivo válido
# inviabiliza a defesa (req. 3.3.7). Então todo arquivo em tests/validos
# precisa carregar E simular, todo arquivo em tests/invalidos precisa ser
# recusado com uma mensagem de erro, e os exemplos do capítulo 2 precisam
# dar o mesmo escalonamento das figuras.

cd "$(dirname "$0")/.." || exit 1
BIN=./projetoSO
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
falhas=0
total=0

ok()    { total=$((total + 1)); printf '  ok    %s\n' "$1"; }
falha() { total=$((total + 1)); falhas=$((falhas + 1)); printf '  FALHA %s\n' "$1"; if [ -n "$2" ]; then printf '        %s\n' "$2"; fi; return 0; }

echo "== Arquivos válidos: devem carregar e simular até o fim"
for f in tests/validos/*.txt; do
    if ! $BIN "$f" --validar --sem-cor > "$TMP/out" 2>&1; then
        falha "$f (validar)" "$(grep erro "$TMP/out" | head -3)"
        continue
    fi
    if $BIN "$f" --modo completo --sem-cor --saida "$TMP/g.svg" > "$TMP/out" 2>&1 \
        && grep -q '</svg>' "$TMP/g.svg"; then
        ok "$f"
    else
        falha "$f (simular)" "$(grep erro "$TMP/out" | head -3)"
    fi
done

echo "== Arquivos inválidos: devem ser recusados com o motivo"
for f in tests/invalidos/*.txt; do
    if $BIN "$f" --validar --sem-cor > "$TMP/out" 2>&1; then
        falha "$f" "foi aceito, mas deveria dar erro"
    elif grep -q 'erro:' "$TMP/out"; then
        ok "$f: $(grep 'erro:' "$TMP/out" | head -1 | sed 's/^erro: //' | cut -c1-90)"
    else
        falha "$f" "recusado sem mensagem de erro"
    fi
done
for f in tests/nao_existe.txt tests; do
    if $BIN "$f" --validar --sem-cor > "$TMP/out" 2>&1; then
        falha "caminho '$f'" "foi aceito"
    else
        ok "caminho '$f': $(head -1 "$TMP/out" | cut -c1-90)"
    fi
done

# Executa em modo completo e devolve a saída em texto. Opções extras vão depois do arquivo.
roda() { arq=$1; shift; $BIN "$arq" --modo completo --texto --sem-cor --saida "$TMP/g.svg" "$@" 2>&1; }

echo "== Avisos"
# Sem nenhum servidor no arquivo, a aperiódica nunca executaria (req. B 1.1):
# o teste de admissão a recusa no ingresso, mesmo com --sem-admissao.
roda tests/validos/08_com_aperiodica.txt > "$TMP/out"
grep -q 't=0 2: recusada (não há servidor' "$TMP/out" && ok "aperiódica sem servidor recusada com o motivo" \
    || falha "aperiódica sem servidor recusada com o motivo"
roda tests/validos/08_com_aperiodica.txt --sem-admissao > "$TMP/out"
grep -q 't=0 T2 recusada pelo teste de admissão' "$TMP/out" && ok "aperiódica sem servidor recusada mesmo com --sem-admissao" \
    || falha "aperiódica sem servidor recusada mesmo com --sem-admissao"

echo "== Figura 2.5 (tabela 2.1 com RM)"
roda exemplos/fig2_5_rm.txt > "$TMP/out"
grep -q 'CPU0: \[0,20)T1 \[20,60)T2 \[60,100)T3 \[100,120)T1 \[120,150)T3 \[150,190)T2 \[190,200)T3 \[200,220)T1' "$TMP/out" \
    && ok "A até 20, B até 60, C preemptada em 100 (A), 150 (B) e 200 (A)" \
    || falha "escalonamento da figura 2.5" "$(grep 'CPU0:' "$TMP/out" | cut -c1-150)"
[ "$(grep -c '10/10' "$TMP/out")" -eq 3 ] && ok "cada tarefa termina após 10 ativações" || falha "10 ativações"

echo "== Figura 2.6 (U = 100%)"
# Com RM, U = 100% não passa no teste de admissão (B seria recusada); para
# ver a perda de prazo da figura, o teste fica desligado.
roda exemplos/fig2_6_rm.txt --sem-admissao > "$TMP/out"
grep -q 't=50 T2 perda de prazo' "$TMP/out" && ok "RM: B perde o prazo em t = 50" || falha "RM: B perde o prazo em t = 50"
roda exemplos/fig2_6_edf.txt > "$TMP/out"
grep -q 't=[0-9]* T[0-9]* perda de prazo' "$TMP/out" && falha "EDF não deveria perder prazo" || ok "EDF: nenhum prazo perdido"
grep -q 'CPU0: \[0,10)T1 \[10,20)T2 \[20,30)T1 \[30,45)T2 \[45,55)T1' "$TMP/out" \
    && ok "EDF: B continua em t = 40 (deadline 50 < 60)" || falha "EDF: escalonamento da figura 2.6"

echo "== Perda de prazo de ativação acumulada (sobrecarga)"
# T2 recebe só 1 de cada 4 ticks e acumula ativações. A 4ª chega em 24 e
# vence em 32 sem nem ter começado: a perda tem que aparecer em t = 32 (o
# deadline), e não quando a ativação finalmente começa a executar.
roda tests/validos/18_ativacoes_acumuladas.txt --sem-admissao > "$TMP/out"
grep -q 't=32 T2 perda de prazo (ativação 4)' "$TMP/out" && grep -q 't=40 T2 perda de prazo (ativação 5)' "$TMP/out" \
    && ok "perda marcada no instante exato do deadline (t = 32 e t = 40)" \
    || falha "perda de ativação acumulada" "$(grep 'perda de prazo' "$TMP/out" | tr '\n' ' ')"

echo "== Várias CPUs, CPU desligada, sorteio e quantum"
roda tests/validos/11_tres_cpus_com_sorteio.txt > "$TMP/out"
grep -q 'CPU2:' "$TMP/out" && ok "3 CPUs na linha do tempo" || falha "3 CPUs"
grep -q 'desligada' "$TMP/out" && ok "CPU desligada aparece" || falha "CPU desligada"
grep -q 'sorteio' "$TMP/out" && ok "sorteio registrado (critério 5)" || falha "sorteio"
roda exemplos/quantum_empate_total.txt > "$TMP/out"
grep -q 'CPU0: \[0,6)T. \[6,12)T.' "$TMP/out" \
    && ok "fim de quantum não tira a tarefa da CPU (critério 1)" || falha "quantum x critério 1"
roda exemplos/quantum_respeita_prazo.txt --sem-admissao > "$TMP/out"
grep -q 'CPU0: \[0,4)T1 \[4,8)T2' "$TMP/out" && grep -q 'Nenhum prazo perdido' "$TMP/out" \
    && ok "fim de quantum não passa por cima do critério 2 (prazo)" || falha "quantum x critério 2"

echo "== Desempate: critério 2 usa o prazo relativo"
# Em t = 12 (T3 acabou), T1 (chegou em 0, prazo 20) e T2 (chegou em 10, prazo
# 10) têm o mesmo deadline absoluto (20), que é a prioridade no EDF. O
# critério 2 compara o prazo relativo (campo 'prazo', ver README "Desempate"):
# 10 < 20, então T2 vem primeiro. Pelo deadline absoluto empataria e o
# critério 3 (ingresso) daria T1.
roda tests/validos/20_desempate_prazo_absoluto.txt --sem-admissao > "$TMP/out"
grep -q 'CPU0: \[0,12)T3 \[12,15)T2 \[15,18)T1' "$TMP/out" \
    && ok "mesmo deadline absoluto: o critério 2 (menor prazo relativo) escolhe T2" \
    || falha "critério 2 (prazo relativo)" "$(grep 'CPU0:' "$TMP/out" | head -1 | cut -c1-100)"

echo "== Desempate: critério 1 quando a próxima ativação chega no fim da anterior"
# T1 e T2 iguais (C = 5, P = 10) numa CPU. Quem executou de 5 a 10 termina a
# ativação em t = 10, no mesmo instante em que chegam as próximas das duas:
# pelo critério 1 ela continua, de 5 a 15. Só o empate de t = 0 (quando
# ninguém executava antes) vai para o sorteio.
roda tests/validos/21_criterio1_chegada_no_fim.txt > "$TMP/out"
[ "$(grep -c 'sorteio (ativação' "$TMP/out")" -eq 1 ] && grep -q 'CPU0: \[0,5)T. \[5,15)T. \[15,25)T.' "$TMP/out" \
    && ok "quem estava executando continua quando a nova ativação chega junto (sem sorteio)" \
    || falha "critério 1 com chegada no fim da ativação" "$(grep 'sorteio (ativação' "$TMP/out" | head -3 | tr '\n' ' ')"

echo "== Determinismo"
roda tests/validos/16_quarenta_tarefas.txt > "$TMP/a"
roda tests/validos/16_quarenta_tarefas.txt > "$TMP/b"
cmp -s "$TMP/a" "$TMP/b" && ok "mesma semente, mesmo resultado" || falha "determinismo"

echo "== Passo a passo: avançar/retroceder e edição"
printf 'g 60\nx %s/ida.svg\ng 150\np 50\np 40\nx %s/volta.svg\nq\n' "$TMP" "$TMP" \
    | $BIN exemplos/fig2_5_rm.txt --modo passo --sem-cor > "$TMP/out" 2>&1
cmp -s "$TMP/ida.svg" "$TMP/volta.svg" && ok "voltar para t = 60 reproduz exatamente o mesmo Gantt" \
    || falha "retroceder deveria reproduzir o estado"
printf 'g 30\ne 1\n6\n0\n5\nq\n' | $BIN exemplos/fig2_6_rm.txt --modo passo --sem-cor --sem-admissao > "$TMP/out" 2>&1
grep -q 'não tem ativação em andamento' "$TMP/out" && ok "edição inválida recusada com motivo" \
    || falha "edição inválida deveria ser recusada"
printf 'g 25\ne 2\n7\nn 10\nt 2\nq\n' | $BIN exemplos/fig2_6_rm.txt --modo passo --sem-cor --sem-admissao > "$TMP/out" 2>&1
grep -q 'suspensa 10 tick' "$TMP/out" && grep -q '░' "$TMP/out" && ok "suspensão aparece no estado e no Gantt" \
    || falha "suspensão"

echo "== Suspensão por período x suspensão pelo usuário (SUSP_PERIODO / SUSP_USUARIO)"
# T1 (fig. 2.5) acaba em t = 20 e só chega de novo em t = 100: nesse intervalo
# ela está suspensa por período. Não é a suspensão do usuário, então não conta
# como tick suspenso e não pode aparecer pontilhada no Gantt.
printf 'g 30\nt 1\nf\nq\n' | $BIN exemplos/fig2_5_rm.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'estado: suspensa: próximo período' "$TMP/out" && grep -q 'suspensa 0 tick(s)' "$TMP/out" \
    && ok "esperar o período = suspensa: próximo período, sem contar tick suspenso" \
    || falha "estado SUSP_PERIODO" "$(grep 'estado:' "$TMP/out" | head -2)"
grep -v 'Legenda' "$TMP/out" | grep -q '░' && falha "esperar o período não pode ser pontilhado no Gantt" \
    || ok "esperar o período fica vazio no Gantt (como nas figuras do capítulo)"
# A próxima ativação (t = 100) tira a tarefa da suspensão sozinha.
printf 'g 101\nt 1\nq\n' | $BIN exemplos/fig2_5_rm.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'estado: executando' "$TMP/out" && ok "a chegada da próxima ativação encerra a suspensão por período" \
    || falha "SUSP_PERIODO deveria acabar sozinha" "$(grep 'estado:' "$TMP/out" | head -2)"
# Suspensa pelo usuário em t = 30: a chegada em t = 100 NÃO a libera; só a retomada.
printf 'g 30\ne 1\n7\ng 110\nt 1\n\ne 1\n7\nq\n' | $BIN exemplos/fig2_5_rm.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'estado: suspensa: pelo usuário' "$TMP/out" && grep -q 'suspensa [1-9][0-9]* tick(s)' "$TMP/out" \
    && ok "suspensão do usuário sobrevive à chegada da ativação e conta ticks" \
    || falha "SUSP_USUARIO não pode ser desfeita pela chegada" "$(grep 'estado:' "$TMP/out" | head -2)"
grep -q 'Tarefa 1 retomada (pronta)' "$TMP/out" && ok "retomar com ativação pendente volta para pronta" \
    || falha "retomada de SUSP_USUARIO" "$(grep 'retomada' "$TMP/out")"

echo "== Id em texto (número ou texto; sem diferenciar maiúsculas)"
# Linhas das tarefas no Gantt do terminal, de cima para baixo (1ª faixa).
linhas_gantt() { grep '│' "$1" | grep -v '^CPU' | awk '{print $1}' | head -"$2" | tr '\n' ' '; }
# Ordem do eixo Y (menor id embaixo): texto em ordem natural, T2 antes de T10.
roda tests/validos/21_ids_texto_ordem_natural.txt > "$TMP/out"
[ "$(linhas_gantt "$TMP/out" 4)" = "T10 T2 T1 T0 " ] && ok "ordem natural: T10, T2, T1, T0 de cima para baixo" \
    || falha "ordem natural dos ids" "$(linhas_gantt "$TMP/out" 4)"
grep -E -q '\)TT|^  t=[0-9]+ TT' "$TMP/out" && falha "rótulo não pode virar TT1" \
    || ok "ids de texto sem o prefixo T na linha do tempo e nos eventos (nada de TT1)"
# Inteiros antes de texto; 0 e T0 são tarefas diferentes e com rótulos diferentes.
roda tests/validos/22_ids_misturados_0_e_T0.txt > "$TMP/out"
[ "$(linhas_gantt "$TMP/out" 6)" = "T1 T0 A 10 2 0 " ] && ok "misturados: 0 < 2 < 10 < A < T0 < T1, rótulos crus" \
    || falha "ordem dos ids misturados" "$(linhas_gantt "$TMP/out" 6)"
roda tests/validos/25_ids_negativos.txt > "$TMP/out"
[ "$(linhas_gantt "$TMP/out" 3)" = "T1 T0 T-1 " ] && ok "inteiros pelo valor: -1 embaixo, rótulos T<id>" \
    || falha "ordem dos ids negativos" "$(linhas_gantt "$TMP/out" 3)"
# '#1;...' é tarefa (tem ';'); '# comentário' sem ';' é ignorado. A&B escapado no SVG.
roda tests/validos/23_id_com_hash_espaco_e_e_comercial.txt > "$TMP/out"
grep -q '#1' "$TMP/out" && grep -q 'Tarefa 2' "$TMP/out" && ok "id '#1' vira tarefa; id com espaço no meio" \
    || falha "id '#1' / 'Tarefa 2'"
grep -q 'A&amp;B' "$TMP/g.svg" && ! grep -q 'A&B' "$TMP/g.svg" && ok "id 'A&B' escapado no SVG" || falha "escape do id no SVG"
# Latin-1 (Bloco de Notas em ANSI): aceito, e o SVG continua UTF-8 válido.
roda tests/validos/24_id_latin1.txt > "$TMP/out"
iconv -f UTF-8 -t UTF-8 "$TMP/g.svg" > /dev/null 2>&1 && ok "id em Latin-1 aceito e SVG em UTF-8 válido" \
    || falha "SVG com id em Latin-1 não é UTF-8 válido"
# Repetidos: T1/t1, 01/1 e T01/T1 são o mesmo id.
for f in 17_id_repetido_maiusculas 18_id_repetido_zero_a_esquerda 19_id_repetido_texto_numero; do
    $BIN tests/invalidos/$f.txt --validar --sem-cor 2>&1 | grep -q 'id repetido' && ok "$f: id repetido" \
        || falha "$f deveria acusar id repetido"
done
# Sugestão de id novo: maior + 1 (inteiros) ou o próximo do prefixo (T0..T10 -> T11).
printf 'n\n\n\n\n\n\n\n\nq\n' | $BIN exemplos/fig2_5_rm.txt --terminal --sem-cor > "$TMP/out" 2>&1
grep -q 'id \[4\]' "$TMP/out" && ok "sugestão com ids 1, 2, 3: 4" || falha "sugestão de id inteiro"
printf 'n\n\n\n\n\n\n\n\nq\n' | $BIN tests/validos/21_ids_texto_ordem_natural.txt --terminal --sem-cor > "$TMP/out" 2>&1
grep -q 'id \[T11\]' "$TMP/out" && ok "sugestão com T0, T1, T2, T10: T11" || falha "sugestão de id texto"
# Comandos com id em texto: t t0 acha T0; t 0 e t T0 são tarefas diferentes.
printf 't t0\n\nq\n' | $BIN tests/validos/21_ids_texto_ordem_natural.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'Tarefa T0 (TCB)' "$TMP/out" && ok "'t t0' acha a tarefa T0" || falha "busca sem diferenciar maiúsculas"
printf 't 0\n\nt T0\n\nq\n' | $BIN tests/validos/22_ids_misturados_0_e_T0.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'Tarefa 0 (TCB)' "$TMP/out" && grep -q 'Tarefa T0 (TCB)' "$TMP/out" && ok "'t 0' e 't T0' são tarefas diferentes" \
    || falha "'t 0' / 't T0'"
# Tarefa de id texto adicionada na simulação some ao voltar para antes dela.
printf 'n 5\n+\nX\n\n\n\n\n\n\nt X\n\np 3\nt X\nq\n' | $BIN exemplos/fig2_5_rm.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'Tarefa X adicionada' "$TMP/out" && grep -q 'Tarefa X (TCB)' "$TMP/out" \
    && grep -q 'não existe tarefa com id X neste instante' "$TMP/out" \
    && ok "tarefa 'X' adicionada no passo a passo e desfeita ao voltar no tempo" || falha "adicionar tarefa de id texto"

echo "== Projeto B: servidores (linhas do tempo calculadas à mão nos comentários dos exemplos)"
# BS: só usa CPU ociosa. Em t = 3 a fila tem A1 (chegou em 1) e E1 (em 2);
# a esporádica passa na frente: E1 de 3 a 5, A1 de 7 a 10 (T1 volta em 5).
roda exemplos/b_background_server.txt > "$TMP/out"
grep -q 'CPU0: \[0,2)1 \[2,3)2 \[3,5)BS1:E1 \[5,7)1 \[7,10)BS1:A1 \[10,12)1' "$TMP/out" \
    && ok "BS: esporádica antes da aperiódica, só nos ticks ociosos" || falha "BS" "$(grep 'CPU0:' "$TMP/out" | head -1 | cut -c1-120)"
grep -q 't=2 E1: aceita sem garantia (só há Background Server' "$TMP/out" \
    && ok "esporádica só com BS: aceita sem garantia" || falha "esporádica só com BS"
# PS (1 a cada 5): em t = 1 ganha a CPU com a fila vazia e descarta; em 5
# atende E1, em 10 uma parte de A1 (a capacidade acaba), em 15 a 2ª de E1.
roda exemplos/b_polling_server.txt > "$TMP/out"
grep -q 'CPU0: \[0,1)1 \[1,3)2 \[3,4)desligada \[4,5)1 \[5,6)PS1:E1 \[6,8)desligada \[8,9)1 \[9,10)2 \[10,11)PS1:A1 \[11,12)2 \[12,13)1 \[13,15)desligada \[15,16)PS1:E1 \[16,17)1 \[17,19)2 \[19,20)desligada \[20,21)1 \[21,22)PS1:A1' "$TMP/out" \
    && ok "PS: capacidade por período, atende na ordem da fila" || falha "PS" "$(grep 'CPU0:' "$TMP/out" | head -1 | cut -c1-120)"
grep -q 't=1 PS1 capacidade descartada' "$TMP/out" && ok "PS: fila vazia ao ganhar a CPU descarta a capacidade" \
    || falha "PS: descarte da capacidade"
# DS (1 a cada 5): guarda a capacidade e atende A1 assim que ela chega (2),
# tirando a CPU de T2 (prioridade 5 contra 8).
roda exemplos/b_deferrable_server.txt > "$TMP/out"
grep -q 'CPU0: \[0,1)1 \[1,2)2 \[2,3)DS1:A1 \[3,4)2 \[4,5)1 \[5,6)DS1:A1 \[6,8)desligada \[8,9)1 \[9,10)2 \[10,11)DS1:A2 \[11,12)2' "$TMP/out" \
    && ok "DS: guarda a capacidade e atende na chegada" || falha "DS" "$(grep 'CPU0:' "$TMP/out" | head -1 | cut -c1-120)"
grep -q 'DS1: aceita (RM, análise do tempo de resposta: .*R = 6)' "$TMP/out" \
    && ok "DS na admissão: jitter T-C na análise do tempo de resposta (R = 6 para T2)" || falha "DS na admissão"
# SS (4 a cada 8, EDF): ativo em 1 (gasta 2, volta em 9), em 4 (gasta 2,
# volta em 12), em 9 (gasta 2, volta em 17) e em 12 (gasta 1, volta em 20).
roda exemplos/b_sporadic_server.txt > "$TMP/out"
grep -q 'CPU0: \[0,1)1 \[1,3)SS1:A1 \[3,5)1 \[5,7)SS1:A2 \[7,9)desligada \[9,11)SS1:A2 \[11,14)1 \[14,15)SS1:A2' "$TMP/out" \
    && ok "SS: executa com a capacidade que tem" || falha "SS" "$(grep 'CPU0:' "$TMP/out" | head -1 | cut -c1-120)"
[ "$(grep -o 't=[0-9]* SS1 recarga' "$TMP/out" | tr '\n' ' ')" = "t=9 SS1 recarga t=12 SS1 recarga t=17 SS1 recarga t=20 SS1 recarga " ] \
    && ok "SS: recargas em 9, 12, 17 e 20 (um período depois de ficar ativo)" \
    || falha "SS: recargas" "$(grep -o 't=[0-9]* SS1 recarga' "$TMP/out" | tr '\n' ' ')"
# Uma esporádica que chega tira a aperiódica do servidor (troca de dono).
printf 'RM;2;1\n1;E74C3C;0;2;20;20\nDS1;95A5A6;0;3;10;10\nA1;F39C12;0;5;0\nE1;2ECC71;2;1;-20;20\n' > "$TMP/troca.txt"
roda "$TMP/troca.txt" > "$TMP/out"
grep -q 'CPU0: \[0,2)DS1:A1 \[2,3)DS1:E1 \[3,5)1' "$TMP/out" \
    && ok "esporádica que chega tira a aperiódica do servidor" || falha "troca no servidor" "$(grep 'CPU0:' "$TMP/out" | head -1 | cut -c1-80)"

echo "== Projeto B: teste de admissão"
# Com o PS1 (2 a cada 5), R de T2 = 2 + teto(R/4)*1 + teto(R/5)*2: 2, 5, 6, 8 > 6.
roda exemplos/b_admissao_recusa.txt > "$TMP/out"
grep -q 't=0 PS1: recusada (RM, análise do tempo de resposta: com ela, 2 teria tempo de resposta 8 > prazo = 6)' "$TMP/out" \
    && ok "PS1 recusado pela análise do tempo de resposta (R = 8 > 6)" || falha "recusa do PS1"
grep -q 't=2 A1: recusada (não há servidor' "$TMP/out" && ok "aperiódica sem servidor (o PS1 foi recusado) também é recusada" \
    || falha "aperiódica sem servidor"
grep -q 'PS1' "$TMP/out" && grep -q 't=0 PS1 recusada pelo teste de admissão' "$TMP/out" && ok "a recusa vira um evento" \
    || falha "evento de recusa"
grep -q 'class="recusa"' "$TMP/g.svg" && ok "a recusa tem marcador no SVG" || falha "marcador de recusa no SVG"
roda exemplos/b_admissao_recusa.txt --sem-admissao > "$TMP/out"
grep -q 'PS1:A1' "$TMP/out" && ! grep -q 'recusada pelo teste' "$TMP/out" && ok "--sem-admissao: tudo entra e o PS1 executa A1" \
    || falha "--sem-admissao"
# Aperiódica com prazo: o PS1 (1 a cada 10) pode ficar 18 ticks sem entregar
# nada; A1 pede 3 em 5 (recusada) e A2 pede 3 em 40 (garantido = 3, aceita).
roda exemplos/b_aperiodica_com_prazo.txt > "$TMP/out"
grep -q 't=0 A1 (ativação 1): recusada' "$TMP/out" && grep -q 't=0 A2 (ativação 1): aceita' "$TMP/out" \
    && ok "ativação de aperiódica com prazo testada ao entrar na fila" || falha "teste da ativação aperiódica"
grep -q 't=21 A2 término' "$TMP/out" && ! grep -q 'A2 perda de prazo' "$TMP/out" \
    && ok "a ativação aceita cumpre o prazo (termina em 21 <= 40)" || falha "prazo da ativação aceita"
roda exemplos/fig2_6_rm.txt > "$TMP/out"
grep -q 't=0 2: recusada (RM, análise do tempo de resposta: ela teria tempo de resposta 55 > prazo = 50)' "$TMP/out" \
    && ok "fig. 2.6 com RM: B recusada (R = 55 > 50)" || falha "fig. 2.6 na admissão"

echo "== Projeto B: Gantt e interface"
roda exemplos/b_polling_server.txt > "$TMP/out"
grep -q 'class="exec serv"' "$TMP/g.svg" && grep -q 'class="exec aper"' "$TMP/g.svg" && grep -q 'id="listras"' "$TMP/g.svg" \
    && ok "SVG: servidor listrado e aperiódica com borda tracejada" || falha "SVG do Projeto B"
printf 'g 4\ns\n\nt PS1\n\nt A1\n\nd\n\nq\n' | $BIN exemplos/b_polling_server.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'fila de aperiódicas: E1 (esp., chegou em 3) A1 (aper., chegou em 2)' "$TMP/out" \
    && ok "passo a passo: fila de aperiódicas na ordem de atendimento (esporádica primeiro)" || falha "fila no comando s"
grep -q 'capacidade restante: 0 de 1' "$TMP/out" && grep -q 'na fila de aperiódicas: posição 2 de 2' "$TMP/out" \
    && ok "passo a passo: TCB do servidor e da aperiódica" || falha "comando t do Projeto B"
grep -q 'Teste de admissão:' "$TMP/out" && ok "passo a passo: comando d lista as decisões" || falha "comando d"
# A edição não pode trocar o tipo; a esporádica confere o intervalo mínimo.
printf 'g 4\ne PS1\n3\n0\n5\ne E1\n3\n-20\n-12\nq\n' | $BIN exemplos/b_polling_server.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'mudar o tipo da tarefa durante a simulação não é permitido' "$TMP/out" \
    && grep -q 'menos que o novo intervalo mínimo (20)' "$TMP/out" \
    && ok "edição: período não troca o tipo; esporádica confere as ocorrências" || falha "edição com tipos"

echo "== Plugin dinâmico"
if [ -f exemplo_fifo.so ]; then
    printf 'FIFO;2;1\n1;E74C3C;0;3;10;10\n2;3498DB;1;3;10;10\n' > "$TMP/fifo.txt"
    if $BIN "$TMP/fifo.txt" --plugin exemplo_fifo.so --modo completo --texto --sem-cor \
        --saida "$TMP/g.svg" > "$TMP/out" 2>&1 && grep -q 'CPU0: \[0,3)T1 \[3,6)T2' "$TMP/out"; then
        ok "escalonador FIFO carregado de exemplo_fifo.so"
    else
        falha "plugin" "$(head -3 "$TMP/out")"
    fi
else
    echo "  (pulado: rode 'make plugins')"
fi

echo
if [ "$falhas" -eq 0 ]; then
    echo "Todos os $total testes passaram."
else
    echo "$falhas de $total testes falharam."
    exit 1
fi
