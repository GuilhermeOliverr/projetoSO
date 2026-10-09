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

# Executa em modo completo e devolve a saída em texto.
roda() { $BIN "$1" --modo completo --texto --sem-cor --saida "$TMP/g.svg" ${2:+"$@"} 2>&1; }

echo "== Avisos"
roda tests/validos/08_com_aperiodica.txt > "$TMP/out"
grep -q 'aperiódica (periodo = 0) e foi ignorada' "$TMP/out" && ok "aperiódica ignorada com aviso" \
    || falha "aperiódica ignorada com aviso"
grep -q 'T2' "$TMP/out" && falha "aperiódica não deveria aparecer no Gantt" || ok "aperiódica fora do Gantt"

echo "== Figura 2.5 (tabela 2.1 com RM)"
roda exemplos/fig2_5_rm.txt > "$TMP/out"
grep -q 'CPU0: \[0,20)T1 \[20,60)T2 \[60,100)T3 \[100,120)T1 \[120,150)T3 \[150,190)T2 \[190,200)T3 \[200,220)T1' "$TMP/out" \
    && ok "A até 20, B até 60, C preemptada em 100 (A), 150 (B) e 200 (A)" \
    || falha "escalonamento da figura 2.5" "$(grep 'CPU0:' "$TMP/out" | cut -c1-150)"
[ "$(grep -c '10/10' "$TMP/out")" -eq 3 ] && ok "cada tarefa termina após 10 ativações" || falha "10 ativações"

echo "== Figura 2.6 (U = 100%)"
roda exemplos/fig2_6_rm.txt > "$TMP/out"
grep -q 't=50 T2 perda de prazo' "$TMP/out" && ok "RM: B perde o prazo em t = 50" || falha "RM: B perde o prazo em t = 50"
roda exemplos/fig2_6_edf.txt > "$TMP/out"
grep -q 't=[0-9]* T[0-9]* perda de prazo' "$TMP/out" && falha "EDF não deveria perder prazo" || ok "EDF: nenhum prazo perdido"
grep -q 'CPU0: \[0,10)T1 \[10,20)T2 \[20,30)T1 \[30,45)T2 \[45,55)T1' "$TMP/out" \
    && ok "EDF: B continua em t = 40 (deadline 50 < 60)" || falha "EDF: escalonamento da figura 2.6"

echo "== Perda de prazo de ativação acumulada (sobrecarga)"
# T2 recebe só 1 de cada 4 ticks e acumula ativações. A 4ª chega em 24 e
# vence em 32 sem nem ter começado: a perda tem que aparecer em t = 32 (o
# deadline), e não quando a ativação finalmente começa a executar.
roda tests/validos/18_ativacoes_acumuladas.txt > "$TMP/out"
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
roda exemplos/quantum_respeita_prazo.txt > "$TMP/out"
grep -q 'CPU0: \[0,4)T1 \[4,8)T2' "$TMP/out" && grep -q 'Nenhum prazo perdido' "$TMP/out" \
    && ok "fim de quantum não passa por cima do critério 2 (prazo)" || falha "quantum x critério 2"

echo "== Determinismo"
roda tests/validos/16_quarenta_tarefas.txt > "$TMP/a"
roda tests/validos/16_quarenta_tarefas.txt > "$TMP/b"
cmp -s "$TMP/a" "$TMP/b" && ok "mesma semente, mesmo resultado" || falha "determinismo"

echo "== Passo a passo: avançar/retroceder e edição"
printf 'g 60\nx %s/ida.svg\ng 150\np 50\np 40\nx %s/volta.svg\nq\n' "$TMP" "$TMP" \
    | $BIN exemplos/fig2_5_rm.txt --modo passo --sem-cor > "$TMP/out" 2>&1
cmp -s "$TMP/ida.svg" "$TMP/volta.svg" && ok "voltar para t = 60 reproduz exatamente o mesmo Gantt" \
    || falha "retroceder deveria reproduzir o estado"
printf 'g 30\ne 1\n6\n0\n5\nq\n' | $BIN exemplos/fig2_6_rm.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'não tem ativação em andamento' "$TMP/out" && ok "edição inválida recusada com motivo" \
    || falha "edição inválida deveria ser recusada"
printf 'g 25\ne 2\n7\nn 10\nt 2\nq\n' | $BIN exemplos/fig2_6_rm.txt --modo passo --sem-cor > "$TMP/out" 2>&1
grep -q 'suspensa 10 tick' "$TMP/out" && grep -q '░' "$TMP/out" && ok "suspensão aparece no estado e no Gantt" \
    || falha "suspensão"

echo "== Plugin dinâmico"
if [ -f plugins/exemplo_fifo.so ]; then
    printf 'FIFO;2;1\n1;E74C3C;0;3;10;10\n2;3498DB;1;3;10;10\n' > "$TMP/fifo.txt"
    if $BIN "$TMP/fifo.txt" --plugin plugins/exemplo_fifo.so --modo completo --texto --sem-cor \
        --saida "$TMP/g.svg" > "$TMP/out" 2>&1 && grep -q 'CPU0: \[0,3)T1 \[3,6)T2' "$TMP/out"; then
        ok "escalonador FIFO carregado de plugins/exemplo_fifo.so"
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
