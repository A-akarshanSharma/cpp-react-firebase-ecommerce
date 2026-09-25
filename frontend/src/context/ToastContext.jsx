import { createContext, useCallback, useContext, useEffect, useRef, useState } from 'react'
const Context = createContext(null)
export function ToastProvider({ children }) {
  const [messages, setMessages] = useState([])
  const timers = useRef([])
  useEffect(() => () => timers.current.forEach(clearTimeout), [])
  const notify = useCallback((text, type = 'success') => {
    const id = crypto.randomUUID()
    setMessages((m) => [...m.slice(-3), { id, text, type }])
    timers.current.push(setTimeout(() => setMessages((m) => m.filter((t) => t.id !== id)), 6500))
  }, [])
  return (
    <Context.Provider value={notify}>
      {children}
      <div className="toasts" aria-live="polite">
        {messages.map((m) => (
          <div className={`toast ${m.type}`} key={m.id}>
            <span>{m.text}</span>
            <button
              aria-label="Dismiss notification"
              onClick={() => setMessages((v) => v.filter((t) => t.id !== m.id))}
            >
              ×
            </button>
          </div>
        ))}
      </div>
    </Context.Provider>
  )
}
export const useToast = () => useContext(Context)
