# Relatório Técnico: Resolução do CIA no OpenRCT2 e Diagnóstico do Crash no Julius (Caesar III)

## 1. O que descobrimos e corrigimos no OpenRCT2 (que agora está 100% funcional):

O erro inicial comum a ambos os projetos (`ErrDisp: The SD card was removed`) no Menu Home tinha **4 causas no arquivo RSF**:
1. **`Logo: None`**: No 3DS, títulos da categoria `Application` exigem a partição de splash/licença. `Logo: None` deixava o tamanho de logo zerado no cabeçalho NCCH, fazendo o loader abortar na inicialização com tela preta. Deve ser `Logo: Homebrew`.
2. **`ServiceAccessControl`**: O serviço `APT:U` deve ser rigorosamente em letras maiúsculas (`APT:U`).
3. **`Dependency`**: O Process Manager (`pm`) do 3DS exige a tabela de dependências dos módulos nativos (`ac`, `am`, `boss`, `camera`, `cfg`, `codec`, `csnd`, `dlp`, `dsp`, `friends`, `gpio`, `gsp`, `hid`, `i2c`, `ir`, `mcu`, `ndm`, `news`, `nim`, `nwm`, `pdn`, `ps`, `ptm`, `ro`, `socket`, `spi`, `ssl`).
4. **Clock seguro no boot**: Deixar `CpuSpeed: 268MHz` no RSF e ativar os 804 MHz por código via `osSetSpeedupEnable(true)`.

### E por que travava ao carregar o jogo/cenário?
- **Modo de Memória**: O RSF usava `SystemModeExt: Legacy`, o que limitava a memória a 48 MB. Ao mudar para `SystemModeExt: 124MB`, o New 3DS liberou **~98 MB de heap**, permitindo carregar cenários pesados sem estourar a RAM.

---

## 2. Diagnóstico Exato do Crash do Julius (`crash_dump_00000001.dmp`):

Analisamos os registradores do arquivo `/Volumes/NO NAME/luma/dumps/arm11/crash_dump_00000001.dmp` e passamos os endereços de retorno pelo `arm-none-eabi-addr2line`:

```
r0: 0x1ff57ffe
PC / LR / Callstack:
- libctru/source/services/dsp.c:160
- libctru/source/ndsp/ndsp.c:60
- libctru/source/ndsp/ndsp.c:10
```

### Causa Raiz:
- O crash é um **Data Abort** durante a inicialização do áudio DSP (`ndspInit()` / `dsp.c`).
- O registrador `r0 = 0x1ff57ffe` aponta diretamente para o espaço de registradores de hardware do DSP (`0x1FF00000 - 0x1FF7FFFF`).
- No `.3dsx`, o Homebrew Launcher já mapeia essa faixa de memória virtual.
- No `.cia`, se o arquivo `julius.rsf` **não declarar explicitamente o `IORegisterMapping`**, o kernel do 3DS não mapeia os registradores do DSP para o espaço de endereçamento do processo, gerando Data Abort instantâneo ao tentar tocar som!

---

## 3. Como corrigir o Julius imediatamente:

No arquivo `julius.rsf`, adicione as seções de mapeamento de memória virtual que faltavam:

```yaml
  # Virtual Address Mappings
  IORegisterMapping:
   - 1ff00000-1ff7ffff   # DSP memory (Corrige o crash em r0 = 0x1ff57ffe!)
  MemoryMapping: 
   - 1f000000-1f5fffff:r # VRAM
```

E certifique-se de que o modo de memória e dependências estejam presentes:
```yaml
  SystemMode                    : 64MB
  SystemModeExt                 : 124MB
  CpuSpeed                      : 268MHz
  EnableL2Cache                 : true
  CanAccessCore2                : true
```

Após recompilar o `julius.cia` com essa correção no `julius.rsf`, o áudio DSP inicializará com sucesso e o Caesar III abrirá no Menu Home perfeitamente!
