# Script para remover versões do PlatformIO Registry
# Execute este script no PowerShell

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Removendo versões do PlatformIO" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Verificar se está logado
Write-Host "Verificando autenticação..." -ForegroundColor Yellow
pio account show

if ($LASTEXITCODE -ne 0) {
    Write-Host "Você não está logado. Execute: pio account login" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Removendo versão 1.0.7..." -ForegroundColor Yellow
pio pkg unpublish thiagozaneti/Modem-a7608sa-library@1.0.7

if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ Versão 1.0.7 removida com sucesso!" -ForegroundColor Green
} else {
    Write-Host "✗ Erro ao remover 1.0.7 (pode estar fora do período de 72h)" -ForegroundColor Red
}

Write-Host ""
Write-Host "Removendo versão 1.0.6..." -ForegroundColor Yellow
pio pkg unpublish thiagozaneti/Modem-a7608sa-library@1.0.6

if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ Versão 1.0.6 removida com sucesso!" -ForegroundColor Green
} else {
    Write-Host "✗ Erro ao remover 1.0.6 (pode estar fora do período de 72h)" -ForegroundColor Red
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Verificando versões restantes..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
pio pkg show thiagozaneti/Modem-a7608sa-library

Write-Host ""
Write-Host "Processo concluído!" -ForegroundColor Green
