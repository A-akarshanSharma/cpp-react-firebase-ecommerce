import { Component, useEffect, useId, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import { Link } from 'react-router-dom'
import { mediaUrl } from '../../utils/media'
export function Icon({ name = 'bag', size = 20, ...props }) {
  const paths = {
    bag: (
      <>
        <path d="M5 7h14l1 14H4L5 7Z" />
        <path d="M9 8V6a3 3 0 0 1 6 0v2" />
      </>
    ),
    search: (
      <>
        <circle cx="10.5" cy="10.5" r="6.5" />
        <path d="m16 16 5 5" />
      </>
    ),
    arrow: <path d="M4 12h16m-6-6 6 6-6 6" />,
    user: (
      <>
        <circle cx="12" cy="7" r="4" />
        <path d="M4 21v-2a8 8 0 0 1 16 0v2" />
      </>
    ),
    box: (
      <>
        <path d="m12 3 9 5-9 5-9-5 9-5Zm-9 5v10l9 5 9-5V8M12 13v10M7 5l10 6" />
      </>
    ),
    check: <path d="m5 12 4 4L19 6" />,
    shield: (
      <>
        <path d="m12 3 8 3v6c0 5-8 9-8 9s-8-4-8-9V6l8-3Z" />
        <path d="m8 12 3 3 5-5" />
      </>
    ),
    sun: (
      <>
        <circle cx="12" cy="12" r="4" />
        <path d="M12 1v3m0 16v3M1 12h3m16 0h3M4 4l2 2m12 12 2 2M4 20l2-2M18 6l2-2" />
      </>
    ),
    menu: <path d="M4 6h16M4 12h16M4 18h16" />,
    grid: (
      <>
        <rect x="3" y="3" width="7" height="7" />
        <rect x="14" y="3" width="7" height="7" />
        <rect x="3" y="14" width="7" height="7" />
        <rect x="14" y="14" width="7" height="7" />
      </>
    ),
    heart: <path d="M20 5c-3-3-6 0-8 2-2-2-5-5-8-2-5 5 8 15 8 15S25 10 20 5Z" />,
    chat: <path d="M21 11a9 9 0 0 1-9 9H3l2-5A9 9 0 1 1 21 11Z" />,
  }
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      {...props}
    >
      {paths[name] || paths.box}
    </svg>
  )
}
export function Button({ children, variant = 'primary', className = '', ...props }) {
  return (
    <button className={`btn ${variant} ${className}`} {...props}>
      {children}
    </button>
  )
}
export function PageHeading({ eyebrow, title, children, action }) {
  return (
    <div className="page-heading">
      <div>
        {eyebrow && <p className="eyebrow">{eyebrow}</p>}
        <h1>{title}</h1>
        {children && <p className="muted">{children}</p>}
      </div>
      {action}
    </div>
  )
}
export function EmptyState({ title, children, to, action = 'Explore the shop' }) {
  return (
    <div className="empty-state">
      <span className="state-icon">
        <Icon name="bag" size={30} />
      </span>
      <h2>{title}</h2>
      <p className="muted">{children}</p>
      {to && (
        <Link className="btn primary" to={to}>
          {action}
          <Icon name="arrow" />
        </Link>
      )}
    </div>
  )
}
export function ErrorState({ message, retry }) {
  return (
    <div className="error-state" role="alert">
      <h3>Something needs a moment</h3>
      <p>{message}</p>
      {retry && (
        <Button variant="secondary" onClick={retry}>
          Try again
        </Button>
      )}
    </div>
  )
}
export function Loader({ cards = false }) {
  return (
    <div role="status" aria-label="Loading" className={cards ? 'product-grid' : 'loading-block'}>
      {cards ? (
        Array.from({ length: 4 }, (_, i) => (
          <div key={i} className="skeleton-card">
            <div className="skeleton" />
            <div className="skeleton line" />
            <div className="skeleton line short" />
          </div>
        ))
      ) : (
        <>
          <span className="spinner" /> Loading…
        </>
      )}
    </div>
  )
}
export function ProductImage({ src, name = 'Product', className = '' }) {
  src = mediaUrl(src)
  const [failed, setFailed] = useState(false)
  useEffect(() => setFailed(false), [src])
  const safe = src && /^(https?:\/\/|\/[^/])/.test(src)
  return safe && !failed ? (
    <img
      className={`product-image ${className}`}
      src={src}
      alt={name}
      loading="lazy"
      onError={() => setFailed(true)}
    />
  ) : (
    <div
      className={`image-placeholder ${className}`}
      role="img"
      aria-label={`${name}: image unavailable`}
    >
      <Icon name="box" size={40} />
      <span>STUDIO THREAD</span>
    </div>
  )
}
export function StatusBadge({ status }) {
  const known = ['pending', 'paid', 'shipped', 'delivered', 'cancelled', 'admin', 'customer']
  return (
    <span className={`status ${known.includes(status) ? status : ''}`}>{status || 'Unknown'}</span>
  )
}
export function QuantityPicker({ value, max, onChange, disabled }) {
  return (
    <div className="quantity-picker">
      <button
        aria-label="Decrease quantity"
        disabled={disabled || value <= 1}
        onClick={() => onChange(value - 1)}
      >
        −
      </button>
      <output aria-label="Quantity">{value}</output>
      <button
        aria-label="Increase quantity"
        disabled={disabled || value >= max}
        onClick={() => onChange(value + 1)}
      >
        +
      </button>
    </div>
  )
}
export function Modal({
  title,
  children,
  onClose,
  onConfirm,
  confirmLabel = 'Confirm',
  busy,
  danger = false,
}) {
  const dialog = useRef(null),
    titleId = useId()
  useEffect(() => {
    const previous = document.activeElement,
      previousOverflow = document.body.style.overflow
    document.body.style.overflow = 'hidden'
    dialog.current?.focus()
    return () => {
      document.body.style.overflow = previousOverflow
      previous?.focus()
    }
  }, [])
  const keyboard = (e) => {
    if (e.key === 'Escape' && !busy) onClose()
    if (e.key === 'Tab') {
      const nodes = [
        ...dialog.current.querySelectorAll(
          'button:not(:disabled),input,select,textarea,a[href],[tabindex="0"]',
        ),
      ]
      const first = nodes[0],
        last = nodes.at(-1)
      if (!first) {
        e.preventDefault()
        return
      }
      if (
        e.shiftKey &&
        (document.activeElement === first || document.activeElement === dialog.current)
      ) {
        e.preventDefault()
        last.focus()
      } else if (
        !e.shiftKey &&
        (document.activeElement === last || document.activeElement === dialog.current)
      ) {
        e.preventDefault()
        first.focus()
      }
    }
  }
  return createPortal(
    <div
      className="modal-backdrop"
      onClick={(e) => {
        if (e.target === e.currentTarget && !busy) onClose()
      }}
    >
      <section
        ref={dialog}
        className="modal"
        role="dialog"
        aria-modal="true"
        aria-labelledby={titleId}
        tabIndex={-1}
        onKeyDown={keyboard}
      >
        <h2 id={titleId}>{title}</h2>
        <div className="modal-body">{children}</div>
        <div className="actions">
          <Button variant="secondary" onClick={onClose} disabled={busy}>
            Cancel
          </Button>
          <Button variant={danger ? 'danger' : 'primary'} onClick={onConfirm} disabled={busy}>
            {busy ? 'Saving…' : confirmLabel}
          </Button>
        </div>
      </section>
    </div>,
    document.body,
  )
}
export class ErrorBoundary extends Component {
  state = { failed: false }
  static getDerivedStateFromError() {
    return { failed: true }
  }
  componentDidCatch(error) {
    if (import.meta.env.DEV) console.error('Page rendering failed:', error.message)
  }
  render() {
    return this.state.failed ? (
      <main className="container section">
        <ErrorState
          message="This page could not be displayed."
          retry={() => window.location.reload()}
        />
        <Link to="/">Return home</Link>
      </main>
    ) : (
      this.props.children
    )
  }
}
