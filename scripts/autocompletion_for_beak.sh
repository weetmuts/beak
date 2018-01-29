# Bash auto completion for beak

_beakusermounts()
{
    local cur=${COMP_WORDS[COMP_CWORD]}
    local uid=$(id -u)
    local mounts=$(cat /proc/mounts | grep fuse.beak | grep user_id=${uid} | cut -f 2 -d ' ' | sort)
    if [ "$mounts" != "" ]; then
        COMPREPLY=($(compgen -W "$mounts" -- $cur))
    fi
    return 0
}

_beakrules()
{
  local cur=${COMP_WORDS[COMP_CWORD]}
  local rules=$(grep -o \\[.*\\] ~/.config/beak/beak.conf | tr -d '[' | tr ']' ':' | sort)
  if [ "$rules" != "" ]; then
      COMPREPLY=($(compgen -W "$rules" -- $cur))
      if [ -z "$COMPREPLY" ]; then
          # No match for configured source trees. Try directories instead.
          _filedir -d
      fi
  fi
  return 0
}

_beakremotes()
{
  local cur=${COMP_WORDS[COMP_CWORD]}
  local prev=${COMP_WORDS[COMP_CWORD-1]}
  if [ "$prev" = ":" ]; then prev=${COMP_WORDS[COMP_CWORD-2]}; fi
  local remotes=$(sed -n "/^\[${prev}\]/,/^\[/p" ~/.config/beak/beak.conf | grep -e 'remote\ *=' | sed 's/remote.*= \?//g' | sort)
  if [ "$remotes" != "" ]; then
      COMPREPLY=($(compgen -W "$remotes" -- $cur))
      if [ -z "$COMPREPLY" ]; then
          # No match for configured remotes trees. Try directories instead.
          _filedir -d
      fi
  fi
  return 0
}

_beak()
{
    local cur prev prevprev prevprevprev
    cur=${COMP_WORDS[COMP_CWORD]}
    prev=${COMP_WORDS[COMP_CWORD-1]}
    prevprev=${COMP_WORDS[COMP_CWORD-2]}
    prevprevprev=${COMP_WORDS[COMP_CWORD-3]}

    # The colon in configured rules gets its own CWORD....
    if [ "$prev" = ":" ]; then prev="$prevprev" ; prevprev="$prevprevprev"; fi

    case "$prev" in
        push) _beakrules ;;
        pull) _beakrules ;;
        prune) _beakrules ;;
        mount) _beakrules ;;
        umount) _beakusermounts ;;
        store) _beakrules ;;
    esac

    case "$prevprev" in
        push) _beakremotes ;;
        pull) _beakremotes ;;
        prune) _beakremotes ;;
        mount) _filedir -d ;;
        store) _filedir -d ;;
    esac

    if [ -z "$COMPREPLY" ]; then
        COMPREPLY=($(compgen -W "push pull prune mount umount store" -- $cur))
    fi
}

complete -F _beak beak
