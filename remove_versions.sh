#!/bin/bash
# Script para remover versões do PlatformIO Registry
# Execute: bash remove_versions.sh

echo "========================================"
echo "  Removendo versões do PlatformIO"
echo "========================================"
echo ""

# Verificar se está logado
echo "Verificando autenticação..."
pio account show

if [ $? -ne 0 ]; then
    echo "❌ Você não está logado. Execute: pio account login"
    exit 1
fi

echo ""
echo "Removendo versão 1.0.7..."
pio pkg unpublish thiagozaneti/Modem-a7608sa-library@1.0.7

if [ $? -eq 0 ]; then
    echo "✅ Versão 1.0.7 removida com sucesso!"
else
    echo "❌ Erro ao remover 1.0.7 (pode estar fora do período de 72h)"
fi

echo ""
echo "Removendo versão 1.0.6..."
pio pkg unpublish thiagozaneti/Modem-a7608sa-library@1.0.6

if [ $? -eq 0 ]; then
    echo "✅ Versão 1.0.6 removida com sucesso!"
else
    echo "❌ Erro ao remover 1.0.6 (pode estar fora do período de 72h)"
fi

echo ""
echo "========================================"
echo "Verificando versões restantes..."
echo "========================================"
pio pkg show thiagozaneti/Modem-a7608sa-library

echo ""
echo "✅ Processo concluído!"
