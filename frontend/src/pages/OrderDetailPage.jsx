import { useCallback, useRef, useState } from 'react'
import { Link, useParams } from 'react-router-dom'
import { api, orderTransitions } from '../services/api'
import { useResource } from '../hooks/useResource'
import { useAuth } from '../context/AuthContext'
import { useCatalog } from '../context/CatalogContext'
import {
  Button,
  ErrorState,
  Loader,
  Modal,
  PageHeading,
  StatusBadge,
} from '../components/common/UI'
import ShipmentForm from '../components/orders/ShipmentForm'
import OrderDetails from '../components/orders/OrderDetails'
import AddressFields, { emptyAddress } from '../components/orders/AddressFields'
export default function OrderDetailPage() {
  const { id } = useParams()
  const { profile } = useAuth()
  const admin = profile?.isAdmin
  const loader = useCallback(() => api.getOrder(id), [id])
  const { data: order, error, loading, reload } = useResource(loader)
  const { reload: reloadCatalog } = useCatalog()
  const [change, setChange] = useState('')
  const [reason, setReason] = useState('')
  const [failure, setFailure] = useState('')
  const [busy, setBusy] = useState(false)
  const [address, setAddress] = useState({ ...emptyAddress })
  const lock = useRef(false)
  const run = async (action) => {
    if (lock.current) return
    lock.current = true
    setBusy(true)
    setFailure('')
    try {
      await action()
      setChange('')
      await reload()
      reloadCatalog()
    } catch (e) {
      setFailure(`${e.message} Refresh the order to check its latest state before retrying.`)
    } finally {
      lock.current = false
      setBusy(false)
    }
  }
  if (loading) return <Loader />
  if (error)
    return (
      <div className="container section">
        <ErrorState message={error} retry={reload} />
      </div>
    )
  return (
    <div className="container section">
      <PageHeading eyebrow="ORDER DETAILS" title={`Order #${order.id}`} />
      <StatusBadge status={order.status} />
      <OrderDetails order={order} />
      {failure && (
        <p className="form-error" role="alert">
          {failure}
        </p>
      )}
      <div className="actions">
        {(admin
          ? orderTransitions[order.status] || []
          : order.status === 'pending'
            ? ['cancelled']
            : []
        ).map((status) => (
          <Button
            key={status}
            variant="secondary"
            disabled={
              busy ||
              (status === 'shipped' &&
                !order.shippingAddress &&
                order.shippingMethod?.kind !== 'pickup')
            }
            onClick={() => {
              setChange(status)
              setFailure('')
            }}
          >
            {status === 'cancelled'
              ? 'Cancel order'
              : `Mark ${order.shippingMethod?.kind === 'pickup' && status === 'shipped' ? 'ready for pickup' : order.shippingMethod?.kind === 'pickup' && status === 'delivered' ? 'collected' : status}`}
          </Button>
        ))}
        <Button variant="text" onClick={reload} disabled={busy}>
          Refresh order
        </Button>
        <Link className="text-link" to={admin ? '/admin/orders' : '/orders'}>
          Back to orders
        </Link>
      </div>
      {admin &&
        order.shippingMethod?.kind !== 'pickup' &&
        !order.shippingAddress &&
        ['pending', 'paid'].includes(order.status) && (
          <form
            onSubmit={(event) => {
              event.preventDefault()
              run(() => api.addOrderAddress(id, address))
            }}
          >
            <p>
              This older order needs a delivery address before shipping. Confirm it with the
              customer; it can only be recorded once.
            </p>
            <AddressFields value={address} onChange={setAddress} disabled={busy} />
            <Button type="submit" disabled={busy}>
              Save order address
            </Button>
          </form>
        )}
      {admin && order.shippingAddress && ['paid', 'shipped'].includes(order.status) && (
        <ShipmentForm key={order.shipment?.version || 'initial'} order={order} onSaved={reload} />
      )}
      {change && (
        <Modal
          title={change === 'cancelled' ? 'Cancel this order?' : `Mark order ${change}?`}
          confirmLabel={change === 'cancelled' ? 'Confirm cancellation' : 'Confirm status'}
          busy={busy}
          onClose={() => setChange('')}
          onConfirm={() =>
            run(() =>
              change === 'cancelled'
                ? api.cancelOrder(id, reason, order.status)
                : api.updateOrderStatus(id, change, order.status),
            )
          }
        >
          <p>
            {change === 'cancelled'
              ? 'Cancellation is final and restores reserved stock. Paid orders require a manual refund review.'
              : change === 'paid'
                ? 'Confirm payment has been received separately. This action does not collect payment.'
                : 'Confirm the order has reached this stage. This action cannot be reversed.'}
          </p>
          {change === 'cancelled' && (
            <label>
              Cancellation reason (optional)
              <textarea
                maxLength={500}
                value={reason}
                onChange={(e) => setReason(e.target.value)}
              />
            </label>
          )}
          {failure && (
            <p className="form-error" role="alert">
              {failure}
            </p>
          )}
        </Modal>
      )}
    </div>
  )
}
