import { useState } from 'react'
import { Link } from 'react-router-dom'
import { api } from '../services/api'
import { usePagedResource } from '../hooks/usePagedResource'
import LoadMore from '../components/common/LoadMore'
import { Button, EmptyState, ErrorState, Loader, PageHeading } from '../components/common/UI'
import { date } from '../utils/format'
export default function NotificationsPage() {
  const resource = usePagedResource(api.getNotifications)
  const { data, loading, error, reload } = resource
  const [failure, setFailure] = useState(''),
    [busy, setBusy] = useState(false)
  return (
    <div className="container section">
      <PageHeading eyebrow="ORDER UPDATES" title="Notifications">
        Order confirmations, status changes and tracking updates.
      </PageHeading>
      <Button variant="text" onClick={reload}>
        Refresh notifications
      </Button>
      {failure && (
        <p role="alert" className="form-error">
          {failure}
        </p>
      )}
      {loading ? (
        <Loader />
      ) : error ? (
        <ErrorState message={error} retry={reload} />
      ) : !data.length ? (
        <EmptyState title="You’re all caught up" />
      ) : (
        <div className="form-stack">
          {[...data]
            .sort((a, b) => b.createdAt - a.createdAt)
            .map((n) => (
              <article className="panel" key={n.id}>
                <p>
                  {!n.read && <strong>New · </strong>}
                  {n.message}
                </p>
                <p className="muted">{date(n.createdAt)}</p>
                <div className="actions">
                  <Link className="text-link" to={`/orders/${n.orderId}`}>
                    View order
                  </Link>
                  {!n.read && (
                    <Button
                      disabled={busy}
                      variant="text"
                      onClick={async () => {
                        setBusy(true)
                        setFailure('')
                        try {
                          await api.readNotification(n.id)
                          await reload()
                        } catch (e) {
                          setFailure(e.message)
                        } finally {
                          setBusy(false)
                        }
                      }}
                    >
                      Mark read
                    </Button>
                  )}
                </div>
              </article>
            ))}
        </div>
      )}
      <LoadMore resource={resource} />
    </div>
  )
}
